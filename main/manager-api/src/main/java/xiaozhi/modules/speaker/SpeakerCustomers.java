package xiaozhi.modules.speaker;

import java.util.LinkedHashMap;
import java.util.Map;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;

/** Administrator projections of Market identities; no local customer passwords or roles. */
@Service
public class SpeakerCustomers {
    private final JdbcTemplate db;
    private final SpeakerSettings settings;
    private final SpeakerGrants grants;
    private final SpeakerMarket market;
    public SpeakerCustomers(JdbcTemplate db,SpeakerSettings settings,SpeakerGrants grants,SpeakerMarket market) {
        this.db=db;this.settings=settings;this.grants=grants;this.market=market;
    }
    public Object list(String keyword,int page,int limit) {
        settings.requireEnabled();
        int size=Math.max(1,Math.min(100,limit)),offset=Math.max(0,Math.min(10000,page-1))*size;
        String search="%"+(keyword==null?"":keyword.strip())+"%";long now=System.currentTimeMillis();
        var rows=db.queryForList("SELECT c.id,c.display_name,c.username,c.account_type,c.balance_status,COUNT(s.device_id) AS device_count,"
                + "COALESCE(SUM(s.last_seen_at_ms>? AND s.last_seen_at_ms<=?),0) AS online_count FROM speaker_customer c "
                + "LEFT JOIN speaker_device_state s ON s.customer_id=c.id WHERE c.display_name LIKE ? OR c.username LIKE ? OR c.subject LIKE ? "
                + "GROUP BY c.id ORDER BY c.created_at_ms DESC,c.id LIMIT ? OFFSET ?",now-75000,now,search,search,search,size,offset);
        long total=db.queryForObject("SELECT COUNT(*) FROM speaker_customer WHERE display_name LIKE ? OR username LIKE ? OR subject LIKE ?",
                Long.class,search,search,search);
        return Map.of("items",rows,"total",total,"page",Math.max(1,page),"limit",size);
    }
    public Map<String,Object> detail(String id) {
        settings.requireEnabled();
        var rows=db.queryForList("SELECT * FROM speaker_customer WHERE id=?",id);
        if(rows.isEmpty()) throw new SpeakerFailure(404,"CUSTOMER_NOT_FOUND");
        var result=new LinkedHashMap<>(rows.getFirst());
        Object organization=result.get("organization");
        try { result.put("organization",organization==null?null:new ObjectMapper().readTree(organization.toString())); }
        catch(Exception invalid) { result.put("organization",null); }
        result.put("devices",db.queryForList("SELECT device_id,name,board_type,firmware_version,last_seen_at_ms,use_enabled,admin_enabled,bound_at_ms,permission_version "
                + "FROM speaker_device_state WHERE customer_id=? ORDER BY device_id",id).stream().map(row -> {
                    var device=new LinkedHashMap<>(row);
                    device.put("online",SpeakerSupport.online(row.get("last_seen_at_ms"),System.currentTimeMillis()));return device;
                }).toList());
        var grant=grants.grant(id);
        result.put("authorization",grant==null?null:Map.of("client_id",settings.clientId(),"token_status",grant.get("token_status"),
                "identity_authorized",SpeakerGrants.usable(grant,System.currentTimeMillis())));
        return result;
    }
    public Map<String,Object> refresh(String id) {
        detail(id);
        try {
            var tokens=grants.currentTokens(id);var profile=market.profile(tokens);
            // Do not overwrite or rotate credentials just to refresh display information.
            db.update("UPDATE speaker_customer SET display_name=?,username=?,account_type=?,organization=?,profile_updated_at_ms=? WHERE id=?",
                    text(profile,"name"),text(profile,"preferred_username"),text(profile,"account_type"),
                    profile.path("organization").isObject()?profile.path("organization").toString():null,System.currentTimeMillis(),id);
        } catch(SpeakerFailure unavailable) { /* Keep the last successful profile and its timestamp. */ }
        grants.refreshBalance(id);
        return detail(id);
    }
    private static String text(com.fasterxml.jackson.databind.JsonNode node,String name) {
        return node.path(name).isTextual()?node.path(name).asText():null;
    }
}
