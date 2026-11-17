package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.time.ZoneId;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.test.util.ReflectionTestUtils;
import xiaozhi.modules.device.controller.OTAController;
import xiaozhi.modules.device.service.DeviceService;
import xiaozhi.modules.device.dto.*;
import xiaozhi.modules.sys.service.SysParamsService;

class SpeakerOtaTest {
    @Test void authenticatedUnboundDeviceUsesTheOriginalFirmwareCatalogueAndBoardMustMatch() {
        var original=mock(DeviceService.class);var devices=mock(SpeakerDevices.class);
        var controller=new OTAController(original,mock(SysParamsService.class));
        ReflectionTestUtils.setField(controller,"speakerControl",true);ReflectionTestUtils.setField(controller,"productTimeZone",ZoneId.of("Asia/Shanghai"));
        ReflectionTestUtils.setField(controller,"speakerDevices",devices);
        var http=new MockHttpServletRequest();http.addHeader("Authorization","Bearer fixture");ReflectionTestUtils.setField(controller,"request",http);
        var identity=new SpeakerDevices.Identity("fixture","hash");
        when(devices.authenticate("Bearer fixture","fixture")).thenReturn(identity);
        when(devices.load(identity,false)).thenReturn(Map.of("board_type","p4"));
        var request=new DeviceReportReqDTO();var board=new DeviceReportReqDTO.BoardInfo();board.setType("p4");request.setBoard(board);
        var app=new DeviceReportReqDTO.Application();app.setVersion("2.2.6");request.setApplication(app);
        var firmware=new DeviceReportRespDTO.Firmware();firmware.setVersion("2.2.7");firmware.setUrl("https://fixture.example/original-download");
        when(original.buildFirmwareInfo("p4","2.2.6")).thenReturn(firmware);
        var response=controller.checkOTAVersion(request,"fixture",null);
        assertEquals(200,response.getStatusCode().value());assertTrue(response.getBody().contains("original-download"));
        verify(original).buildFirmwareInfo("p4","2.2.6");verify(devices).heartbeat(identity,"p4","2.2.6");
        clearInvocations(original);board.setType("other-board");
        assertEquals(400,controller.checkOTAVersion(request,"fixture",null).getStatusCode().value());
        verifyNoInteractions(original);
    }
}
