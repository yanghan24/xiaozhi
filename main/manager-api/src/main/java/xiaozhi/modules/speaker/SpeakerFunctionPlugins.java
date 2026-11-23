package xiaozhi.modules.speaker;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.springframework.dao.DuplicateKeyException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import xiaozhi.modules.agent.entity.AgentPluginMapping;

@Service
public class SpeakerFunctionPlugins {
    private static final String WEATHER = "get_weather";
    private static final String HOME_ASSISTANT = "hass_state";
    private static final String HOME_MUSIC = "hass_play_music";
    private static final TypeReference<Map<String, Object>> PARAMETERS = new TypeReference<>() {};
    private static final TypeReference<List<Field>> FIELDS = new TypeReference<>() {};

    public record Field(String key, String type, String label, Object defaultValue) {
        @com.fasterxml.jackson.annotation.JsonCreator
        public Field(@com.fasterxml.jackson.annotation.JsonProperty("key") String key,
                     @com.fasterxml.jackson.annotation.JsonProperty("type") String type,
                     @com.fasterxml.jackson.annotation.JsonProperty("label") String label,
                     @com.fasterxml.jackson.annotation.JsonProperty("default") Object defaultValue) {
            this.key = key;
            this.type = type;
            this.label = label;
            this.defaultValue = defaultValue;
        }
    }

    public record Input(boolean enabled, long version, Map<String, Object> params, String credential, boolean clearCredential) {
        @Override public String toString() { return "PluginInput[redacted]"; }
    }

    public record Entry(String id, String name, String provider, List<Field> fields, Map<String, Object> params,
                        boolean enabled, boolean ready, boolean hasCredential, boolean legacyConflict, long version) {}
    public record Choice(String id, String name, boolean ready) {}
    private record Definition(String id, String name, String provider, List<Field> fields) {}
    private record Global(boolean enabled, long version, Map<String, Object> params, boolean conflict, boolean persisted) {}

    private final JdbcTemplate db;
    private final ObjectMapper json;
    private final SpeakerCrypto crypto;
    private final SpeakerThirdPartyServices thirdParty;

    public SpeakerFunctionPlugins(JdbcTemplate db, ObjectMapper json, SpeakerCrypto crypto, SpeakerThirdPartyServices thirdParty) {
        this.db = db;
        this.json = json;
        this.crypto = crypto;
        this.thirdParty = thirdParty;
    }

    public List<Entry> list() {
        var definitions = definitions();
        var globals = globals(definitions);
        boolean weatherReady = thirdParty.qweatherReady();
        List<Entry> result = new ArrayList<>();
        for (Definition definition : definitions.values()) {
            Global global = globals.get(definition.provider());
            var fields = visibleFields(definition);
            var params = new LinkedHashMap<String, Object>();
            for (Field field : fields) {
                if (!secret(field.key()) && global.params().containsKey(field.key()))
                    params.put(field.key(), global.params().get(field.key()));
            }
            result.add(new Entry(definition.id(), definition.name(), definition.provider(), fields, params,
                    global.enabled(), ready(definition.provider(), globals, weatherReady),
                    keyPresent(global.params().get("api_key")), global.conflict(), global.version()));
        }
        return result;
    }

    public List<Choice> choices() {
        return list().stream().map(entry -> new Choice(entry.id(), entry.name(), entry.ready())).toList();
    }

    @Transactional
    public void save(String id, Input input) {
        var definitions = definitions();
        var definition = definitions.values().stream().filter(row -> row.id().equals(id)).findFirst()
                .orElseThrow(() -> new SpeakerFailure(404, "PLUGIN_NOT_FOUND"));
        if (input == null || input.params() == null || input.version() < 0)
            throw invalid();
        var globals = globals(definitions);
        Global previous = globals.get(definition.provider());
        if (previous.version() != input.version()) throw changed();

        Map<String, Object> params = new LinkedHashMap<>();
        List<Field> fields = visibleFields(definition);
        if (!fields.stream().filter(field -> !secret(field.key())).map(Field::key).toList().containsAll(input.params().keySet()))
            throw invalid();
        input.params().forEach((name, value) -> {
            if (!validParameter(value)) throw invalid();
            params.put(name, value);
        });
        boolean acceptsCredential = fields.stream().anyMatch(field -> secret(field.key()));
        boolean replacing = input.credential() != null && !input.credential().isBlank();
        if ((replacing || input.clearCredential()) && !acceptsCredential) throw invalid();
        if (replacing && (input.clearCredential() || !validParameter(input.credential()) || !keyPresent(input.credential())))
            throw invalid();
        boolean sameService = java.util.Objects.equals(previous.params().get("provider"), params.get("provider"))
                && java.util.Objects.equals(previous.params().get("base_url"), params.get("base_url"));
        if (acceptsCredential && !input.clearCredential()) {
            Object credential = replacing ? input.credential().strip() : sameService ? previous.params().get("api_key") : null;
            if (keyPresent(credential)) params.put("api_key", credential);
        }
        if (input.enabled() && !validConfiguration(definition.provider(), params)) throw invalid();

        String encoded = write(params);
        if (encoded.getBytes(java.nio.charset.StandardCharsets.UTF_8).length > 24000) throw invalid();
        String cipher = crypto.encryptThirdPartyCredential("plugin:" + id, encoded);
        if (previous.persisted()) {
            if (db.update("""
                    UPDATE speaker_function_plugin SET config_cipher=?, enabled=?, version=version+1
                    WHERE plugin_id=? AND version=?
                    """, cipher, input.enabled(), id, input.version()) != 1) throw changed();
        } else {
            try {
                db.update("INSERT INTO speaker_function_plugin(plugin_id,config_cipher,enabled,version) VALUES (?,?,?,1)",
                        id, cipher, input.enabled());
            } catch (DuplicateKeyException race) {
                throw changed();
            }
        }
        db.update("UPDATE speaker_runtime SET revision=revision+1 WHERE id=1");
    }

    public void validateSelections(List<String> selected, Set<String> existing) {
        Set<String> ready = new java.util.HashSet<>();
        choices().stream().filter(Choice::ready).forEach(choice -> ready.add(choice.id()));
        if (selected.size() > 64 || selected.stream().anyMatch(id -> id == null || (!existing.contains(id) && !ready.contains(id))))
            throw new SpeakerFailure(409, "PLUGIN_NOT_READY");
    }

    public Map<String, Object> runtime(List<AgentPluginMapping> selection) {
        if (selection.isEmpty()) return Map.of();
        var definitions = definitions();
        var globals = globals(definitions);
        boolean weatherReady = selection.stream().anyMatch(mapping -> WEATHER.equals(mapping.getProviderCode()))
                && thirdParty.qweatherReady();
        Map<String, Object> result = new LinkedHashMap<>();
        for (AgentPluginMapping mapping : selection) {
            String provider = mapping.getProviderCode();
            Definition definition = definitions.get(provider);
            if (definition == null) continue;
            Global config = globals.get(provider);
            if (ready(provider, globals, weatherReady)) {
                Map<String, Object> params = HOME_MUSIC.equals(provider) ? globals.get(HOME_ASSISTANT).params() : config.params();
                result.put(provider, write(params));
            } else if (!WEATHER.equals(provider) && !config.persisted() && config.conflict()) {
                var legacy = new LinkedHashMap<>(defaults(definition));
                legacy.putAll(read(mapping.getParamInfo()));
                if (validConfiguration(provider, legacy)) result.put(provider, write(legacy));
            }
        }
        var assistant = globals.get(HOME_ASSISTANT);
        if (assistant != null && assistant.conflict() && globals.containsKey(HOME_MUSIC)
                && globals.get(HOME_MUSIC).enabled() && result.containsKey(HOME_ASSISTANT)
                && selection.stream().anyMatch(mapping -> HOME_MUSIC.equals(mapping.getProviderCode()))) {
            result.putIfAbsent(HOME_MUSIC, "{}");
        }
        Object homeConfig = result.getOrDefault(HOME_ASSISTANT, result.get(HOME_MUSIC));
        if (homeConfig != null) result.put("home_assistant", homeConfig);
        return result;
    }

    public boolean weatherSelected(String agentId) {
        if (!thirdParty.qweatherReady()) return false;
        return Boolean.TRUE.equals(db.queryForObject("""
                SELECT EXISTS(SELECT 1 FROM ai_agent_plugin_mapping m
                    WHERE m.plugin_id='SYSTEM_PLUGIN_WEATHER' AND m.agent_id=?)
                AND COALESCE((SELECT enabled FROM speaker_function_plugin WHERE plugin_id='SYSTEM_PLUGIN_WEATHER'), TRUE)
                """, Boolean.class, agentId));
    }

    private Map<String, Definition> definitions() {
        var definitions = new LinkedHashMap<String, Definition>();
        db.query("""
                SELECT id,name,provider_code,fields FROM ai_model_provider WHERE model_type='Plugin' ORDER BY sort,id
                """, row -> {
            String provider = row.getString("provider_code");
            definitions.put(provider, new Definition(row.getString("id"), row.getString("name"), provider, readFields(row.getString("fields"))));
        });
        return definitions;
    }

    private Map<String, Global> globals(Map<String, Definition> definitions) {
        Map<String, Global> groups = new LinkedHashMap<>();
        Map<String, Definition> byId = new LinkedHashMap<>();
        definitions.values().forEach(definition -> byId.put(definition.id(), definition));
        db.query("""
                SELECT plugin_id,enabled,version,config_cipher FROM speaker_function_plugin
                """, row -> {
            String id = row.getString(1);
            var definition = byId.get(id);
            if (definition != null)
                groups.put(definition.provider(), new Global(row.getBoolean(2), row.getLong(3),
                        read(crypto.decryptThirdPartyCredential("plugin:" + id, row.getString(4))), false, true));
        });
        Map<String, List<Map<String, Object>>> legacy = new LinkedHashMap<>();
        db.query("""
                SELECT DISTINCT p.provider_code,m.param_info
                FROM ai_agent_plugin_mapping m JOIN ai_model_provider p ON p.id=m.plugin_id AND p.model_type='Plugin'
                WHERE p.provider_code<>'get_weather' AND JSON_LENGTH(m.param_info)>0
                """, row -> {
            String provider = row.getString(1);
            if (groups.containsKey(provider)) return;
            var base = new LinkedHashMap<>(defaults(definitions.get(provider)));
            read(row.getString(2)).forEach((key, value) -> {
                if (value != null && (!secret(key) || keyPresent(value))) base.put(key, value);
            });
            var candidates = legacy.computeIfAbsent(provider, ignored -> new ArrayList<>());
            if (candidates.size() < 2 && !candidates.contains(base)) candidates.add(base);
        });
        for (Definition definition : definitions.values()) {
            String provider = definition.provider();
            groups.computeIfAbsent(provider, ignored -> {
                var candidates = legacy.getOrDefault(provider, List.of());
                boolean conflict = candidates.size() > 1;
                var defaults = candidates.size() == 1 ? candidates.getFirst() : defaults(definition);
                return new Global(!"call_device".equals(provider), 0, defaults, conflict, false);
            });
        }
        return groups;
    }

    private static boolean ready(String provider, Map<String, Global> all, boolean weatherReady) {
        Global global = all.get(provider);
        if (!global.enabled() || global.conflict()) return false;
        return switch (provider) {
            case WEATHER -> weatherReady;
            case HOME_MUSIC -> all.containsKey(HOME_ASSISTANT) && !all.get(HOME_ASSISTANT).conflict()
                    && validConfiguration(HOME_ASSISTANT, all.get(HOME_ASSISTANT).params());
            default -> validConfiguration(provider, global.params());
        };
    }

    private static boolean validConfiguration(String provider, Map<String, Object> params) {
        return switch (provider) {
            case HOME_ASSISTANT -> httpUrl(params.get("base_url")) && keyPresent(params.get("api_key"))
                    && params.get("devices") instanceof List<?> devices && !devices.isEmpty();
            case "get_news_from_chinanews" -> httpUrl(params.get("default_rss_url"))
                    && httpUrl(params.get("society_rss_url")) && httpUrl(params.get("world_rss_url"))
                    && httpUrl(params.get("finance_rss_url"));
            case "get_news_from_newsnow" -> httpUrl(params.get("url"))
                    && params.get("news_sources") instanceof String sources && !sources.isBlank();
            case "web_search" -> Set.of("metaso", "tavily", "serply", "outcrawl").contains(String.valueOf(params.get("provider")))
                    && keyPresent(params.get("api_key")) && String.valueOf(params.get("max_results")).matches("[1-9]|1[0-9]|20");
            default -> true;
        };
    }

    private static List<Field> visibleFields(Definition definition) {
        if (WEATHER.equals(definition.provider())) return List.of();
        return definition.fields().stream().map(field -> new Field(field.key(), field.type(), field.label(), null)).toList();
    }

    private static Map<String, Object> defaults(Definition definition) {
        var parameters = new LinkedHashMap<String, Object>();
        if (WEATHER.equals(definition.provider())) return parameters;
        for (Field field : definition.fields()) {
            if (!secret(field.key()) && field.defaultValue() != null) parameters.put(field.key(), field.defaultValue());
        }
        if ("get_news_from_chinanews".equals(definition.provider())) {
            parameters.putIfAbsent("default_rss_url", "https://www.chinanews.com.cn/rss/society.xml");
        }
        return parameters;
    }

    private static boolean secret(String name) {
        return "api_key".equals(name);
    }

    private static boolean keyPresent(Object key) {
        return key instanceof String value && !value.isBlank() && !value.toUpperCase(java.util.Locale.ROOT).contains("XXXX")
                && !value.startsWith("your_");
    }

    private static boolean validParameter(Object value) {
        if (value instanceof String text) return text.length() <= 2048 && text.chars().noneMatch(Character::isISOControl);
        if (value instanceof List<?> list) return list.size() <= 100
                && list.stream().allMatch(item -> item instanceof String name && validParameter(name));
        return value instanceof Number || value instanceof Boolean;
    }

    private static boolean httpUrl(Object value) {
        if (!(value instanceof String text) || text.length() > 1024) return false;
        try {
            var uri = URI.create(text.replace("{source}", "source"));
            return ("https".equals(uri.getScheme()) || "http".equals(uri.getScheme()))
                    && uri.getHost() != null && uri.getUserInfo() == null && uri.getFragment() == null;
        } catch (IllegalArgumentException invalid) {
            return false;
        }
    }

    private Map<String, Object> read(String value) {
        try { return value == null ? Map.of() : json.readValue(value, PARAMETERS); }
        catch (JsonProcessingException badData) { throw new IllegalStateException("Invalid stored function plugin configuration"); }
    }

    private List<Field> readFields(String value) {
        try { return value == null ? List.of() : json.readerFor(FIELDS)
                .without(com.fasterxml.jackson.databind.DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES).readValue(value); }
        catch (JsonProcessingException badData) { throw new IllegalStateException("Invalid stored function plugin fields"); }
    }

    private String write(Map<String, Object> value) {
        try { return json.writeValueAsString(value); }
        catch (JsonProcessingException invalid) { throw invalid(); }
    }

    private static SpeakerFailure invalid() { return new SpeakerFailure(400, "INVALID_PLUGIN_CONFIGURATION"); }
    private static SpeakerFailure changed() { return new SpeakerFailure(409, "PLUGIN_CONFIGURATION_CHANGED"); }
}
