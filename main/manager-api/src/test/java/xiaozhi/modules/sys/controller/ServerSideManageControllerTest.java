package xiaozhi.modules.sys.controller;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.util.*;
import java.util.function.Predicate;
import org.junit.jupiter.api.Test;
import org.mockito.Answers;
import org.mockito.ArgumentCaptor;
import org.springframework.web.socket.WebSocketHttpHeaders;
import xiaozhi.common.exception.RenException;
import xiaozhi.modules.sys.service.*;
import xiaozhi.modules.sys.dto.EmitSeverActionDTO;
import xiaozhi.modules.sys.enums.ServerActionEnum;
import xiaozhi.modules.sys.utils.WebSocketClientManager;
class ServerSideManageControllerTest {
    @Test void authenticatesAsServiceAndRequiresMatchingSuccessAcknowledgement() throws Exception {
        var params=mock(SysParamsService.class);when(params.getValue("server.secret",true)).thenReturn("fixture-service-secret");
        var endpoints=mock(VoiceServiceEndpoints.class);
        when(endpoints.managementUrl("wss://voice.example/xiaozhi/v1/")).thenReturn("ws://127.0.0.1:8000/xiaozhi/management/");
        var controller=new ServerSideManageController(params,endpoints);
        var request=new EmitSeverActionDTO();request.setTargetWs("wss://voice.example/xiaozhi/v1/");request.setAction(ServerActionEnum.UPDATE_CONFIG);
        var client=mock(WebSocketClientManager.class);
        try(var builders=mockConstruction(WebSocketClientManager.Builder.class,withSettings().defaultAnswer(Answers.RETURNS_SELF),
                (mock,context)->when(mock.build()).thenReturn(client))) {
            for(String reply:List.of(
                    "{\"type\":\"server\",\"status\":\"success\",\"content\":{\"action\":\"update_config\"}}",
                    "{\"type\":\"server\",\"status\":\"error\",\"content\":{\"action\":\"update_config\"}}",
                    "{\"type\":\"server\",\"status\":\"success\",\"content\":{\"action\":\"restart\"}}", "invalid")) {
                doAnswer(inv->{((Predicate<String>)inv.getArgument(0)).test(reply);return List.of(reply);}).when(client).listener(any());
                if(reply.contains("success") && reply.contains("update_config")) assertEquals(true,controller.emitServerAction(request).getData());
                else assertThrows(RenException.class,()->controller.emitServerAction(request));
            }
            var headers=ArgumentCaptor.forClass(WebSocketHttpHeaders.class);verify(builders.constructed().getFirst()).headers(headers.capture());
            assertEquals("Bearer fixture-service-secret",headers.getValue().getFirst("Authorization"));
            assertFalse(headers.getValue().containsKey("Device-Id"));
            verify(builders.constructed().getFirst()).uri("ws://127.0.0.1:8000/xiaozhi/management/");
        }
    }
}
