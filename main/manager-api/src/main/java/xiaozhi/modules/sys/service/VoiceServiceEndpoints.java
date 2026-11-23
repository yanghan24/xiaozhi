package xiaozhi.modules.sys.service;

import java.net.URI;
import java.util.*;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;
import xiaozhi.common.constant.Constant;
import xiaozhi.modules.speaker.SpeakerFailure;

/** One device-address source. Optional private mappings only change the management route. */
@Component
public class VoiceServiceEndpoints {
    private final SysParamsService parameters;
    private final Map<String,String> management;
    public VoiceServiceEndpoints(SysParamsService parameters,
            @Value("${speaker.management-url-map:{}}") String mappings) {
        this.parameters=parameters;
        try { management=Map.copyOf(new ObjectMapper().readValue(mappings,new TypeReference<Map<String,String>>(){})); }
        catch(Exception invalid) { throw new IllegalArgumentException("Invalid voice management address mapping"); }
        for(var item:management.entrySet()) {
            if(!valid(item.getKey(),"/xiaozhi/v1/",true) || !valid(item.getValue(),"/xiaozhi/management/",false))
                throw new IllegalArgumentException("Invalid voice management address mapping");
        }
    }
    private static boolean valid(String value,String path,boolean device) {
        try {
            var uri=URI.create(value);
            boolean scheme="wss".equals(uri.getScheme()) || "ws".equals(uri.getScheme())
                    && (!device || "127.0.0.1".equals(uri.getHost()));
            return scheme && uri.getHost()!=null && path.equals(uri.getPath()) && uri.getUserInfo()==null
                    && uri.getQuery()==null && uri.getFragment()==null;
        } catch(Exception invalid) { return false; }
    }
    public List<String> addresses() {
        String configured=parameters.getValue(Constant.SERVER_WEBSOCKET,true);
        if(configured==null || configured.isBlank()) return List.of();
        return Arrays.stream(configured.split(";")).map(String::strip).filter(value->!value.equalsIgnoreCase("null"))
                .filter(value->valid(value,"/xiaozhi/v1/",true)).distinct().toList();
    }
    public String deviceUrl() {
        return addresses().stream().findFirst().orElseThrow(()->new SpeakerFailure(503,"VOICE_NOT_CONFIGURED"));
    }
    public String managementUrl(String address) {
        if(!addresses().contains(address)) throw new SpeakerFailure(400,"VOICE_TARGET_NOT_CONFIGURED");
        return management.getOrDefault(address,address.substring(0,address.length()-"/xiaozhi/v1/".length())+"/xiaozhi/management/");
    }
    public List<Map<String,String>> services() {
        return addresses().stream().map(address->Map.of("address",address,"managementAddress",managementUrl(address),
                "status","not_checked")).toList();
    }
}
