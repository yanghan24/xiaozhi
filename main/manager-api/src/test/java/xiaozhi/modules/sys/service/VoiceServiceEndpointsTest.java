package xiaozhi.modules.sys.service;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import org.junit.jupiter.api.Test;
import xiaozhi.modules.speaker.SpeakerFailure;
class VoiceServiceEndpointsTest {
    @Test void filtersPlaceholdersAndUsesOneSourceForDeviceAndAdministration() {
        var params=mock(SysParamsService.class);
        when(params.getValue("server.websocket",true)).thenReturn(" null ; ; ws://127.0.0.1:8000/xiaozhi/v1/; ws://127.0.0.1:8000/xiaozhi/v1/; https://invalid ");
        var endpoints=new VoiceServiceEndpoints(params,"{}");
        assertEquals(1,endpoints.addresses().size());
        assertEquals(endpoints.deviceUrl(),endpoints.services().getFirst().get("address"));
        assertEquals("ws://127.0.0.1:8000/xiaozhi/management/",endpoints.managementUrl(endpoints.deviceUrl()));
        when(params.getValue("server.websocket",true)).thenReturn("wss://voice.example/xiaozhi/v1/");
        assertEquals("wss://voice.example/xiaozhi/v1/",endpoints.deviceUrl());
    }
    @Test void missingAddressIsExplicitAndArbitraryTargetsCannotBeInvoked() {
        var params=mock(SysParamsService.class);when(params.getValue(anyString(),eq(true))).thenReturn("null");
        var endpoints=new VoiceServiceEndpoints(params,"{}");
        assertTrue(endpoints.services().isEmpty());
        assertThrows(SpeakerFailure.class,endpoints::deviceUrl);
        assertThrows(SpeakerFailure.class,()->endpoints.managementUrl("ws://arbitrary/xiaozhi/v1/"));
    }
    @Test void privateManagementMappingDoesNotChangeDeviceAddress() {
        var params=mock(SysParamsService.class);when(params.getValue(anyString(),eq(true))).thenReturn("wss://voice.example/xiaozhi/v1/");
        var endpoints=new VoiceServiceEndpoints(params,"{\"wss://voice.example/xiaozhi/v1/\":\"ws://127.0.0.1:8000/xiaozhi/management/\"}");
        assertEquals("wss://voice.example/xiaozhi/v1/",endpoints.deviceUrl());
        assertEquals("ws://127.0.0.1:8000/xiaozhi/management/",endpoints.managementUrl(endpoints.deviceUrl()));
        assertThrows(IllegalArgumentException.class,()->new VoiceServiceEndpoints(params,"{\"wss://voice.example/xiaozhi/v1/\":\"file:///tmp/secret\"}"));
    }
}
