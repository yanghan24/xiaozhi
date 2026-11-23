package xiaozhi.modules.device.controller;

import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.time.ZoneId;
import java.util.Map;

import org.apache.commons.lang3.StringUtils;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestHeader;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.ObjectMapper;

import io.swagger.v3.oas.annotations.Hidden;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.enums.ParameterIn;
import io.swagger.v3.oas.annotations.tags.Tag;
import lombok.RequiredArgsConstructor;
import lombok.SneakyThrows;
import lombok.extern.slf4j.Slf4j;
import xiaozhi.common.constant.Constant;
import xiaozhi.common.utils.JsonUtils;
import xiaozhi.modules.device.dto.DeviceReportReqDTO;
import xiaozhi.modules.device.dto.DeviceReportRespDTO;
import xiaozhi.modules.device.entity.DeviceEntity;
import xiaozhi.modules.device.service.DeviceService;
import xiaozhi.modules.sys.service.SysParamsService;

@Tag(name = "设备管理", description = "OTA 相关接口")
@Slf4j
@RestController
@RequiredArgsConstructor
@RequestMapping("/ota/")
public class OTAController {
    @org.springframework.beans.factory.annotation.Value("${device-control.enabled:false}")
    private boolean productControl;
    @org.springframework.beans.factory.annotation.Value("${speaker.enabled:false}")
    private boolean speakerControl;
    @org.springframework.beans.factory.annotation.Value("${device-control.public-origin:${PUBLIC_ORIGIN:}}")
    private String productOrigin;
    @org.springframework.beans.factory.annotation.Value("${device-control.time-zone:Asia/Shanghai}")
    private ZoneId productTimeZone;
    private final DeviceService deviceService;
    private final SysParamsService sysParamsService;
    @jakarta.annotation.Resource private xiaozhi.modules.speaker.SpeakerDevices speakerDevices;
    @jakarta.annotation.Resource private jakarta.servlet.http.HttpServletRequest request;

    @Operation(summary = "OTA版本和设备激活状态检查")
    @PostMapping
    public ResponseEntity<String> checkOTAVersion(
            @RequestBody DeviceReportReqDTO deviceReportReqDTO,
            @Parameter(name = "Device-Id", description = "设备唯一标识", required = true, in = ParameterIn.HEADER) @RequestHeader("Device-Id") String deviceId,
            @Parameter(name = "Client-Id", description = "客户端标识", required = false, in = ParameterIn.HEADER) @RequestHeader(value = "Client-Id", required = false) String clientId) {
        if (speakerControl) {
            try {
                var identity=speakerDevices.authenticate(request.getHeader("Authorization"),deviceId);
                String board=deviceReportReqDTO.getBoard()==null?null:deviceReportReqDTO.getBoard().getType();
                String version=deviceReportReqDTO.getApplication()==null?null:deviceReportReqDTO.getApplication().getVersion();
                var device=speakerDevices.load(identity,false);
                if(board==null || version==null || board.length()>128 || version.length()>128
                        || device.get("board_type")!=null && !device.get("board_type").toString().isBlank() && !board.equals(device.get("board_type")))
                    throw new xiaozhi.modules.speaker.SpeakerFailure(400,"INVALID_DEVICE_REPORT");
                var result=new DeviceReportRespDTO();
                result.setFirmware(deviceService.buildFirmwareInfo(board,version));
                var time=new DeviceReportRespDTO.ServerTime();time.setTimestamp(System.currentTimeMillis());
                time.setTimeZone(productTimeZone.getId());
                time.setTimezone_offset(productTimeZone.getRules().getOffset(Instant.now()).getTotalSeconds()/60);
                result.setServer_time(time);
                speakerDevices.heartbeat(identity,board,version);
                return createResponse(result);
            } catch(xiaozhi.modules.speaker.SpeakerFailure failure) {
                return ResponseEntity.status(failure.status).contentType(MediaType.APPLICATION_JSON).body(JsonUtils.toJsonString(Map.of("error",failure.getMessage())));
            }
        }
        if (productControl) {
            // Discovery is public; only the independent control credential can obtain voice access.
            long timestamp = System.currentTimeMillis();
            // Firmware expects UTC epoch milliseconds plus a separate offset in minutes.
            var discovery = Map.of(
                    "device_control", Map.of("version", speakerControl ? 2 : 1,
                            "base_url", productOrigin + (speakerControl ? "/xiaozhi/speaker/v1/" : "/xiaozhi/device-control/v1/")),
                    "server_time", Map.of("timestamp", timestamp,
                            "timezone_offset", productTimeZone.getRules().getOffset(Instant.ofEpochMilli(timestamp)).getTotalSeconds() / 60,
                            "timeZone", productTimeZone.getId()));
            return ResponseEntity.ok().contentType(MediaType.APPLICATION_JSON).header("Cache-Control","no-store")
                    .body(JsonUtils.toJsonString(discovery));
        }
        if (StringUtils.isBlank(deviceId)) {
            return createResponse(DeviceReportRespDTO.createError("Device ID is required"));
        }
        if (StringUtils.isBlank(clientId)) {
            clientId = deviceId;
        }
        boolean macAddressValid = isMacAddressValid(deviceId);
        // 设备Id和Mac地址应是一致的, 并且必须需要application字段
        if (!macAddressValid) {
            return createResponse(DeviceReportRespDTO.createError("Invalid device ID"));
        }
        return createResponse(deviceService.checkDeviceActive(deviceId, clientId, deviceReportReqDTO));
    }

    @Operation(summary = "设备快速检查激活状态")
    @PostMapping("activate")
    public ResponseEntity<String> activateDevice(
            @Parameter(name = "Device-Id", description = "设备唯一标识", required = true, in = ParameterIn.HEADER) @RequestHeader("Device-Id") String deviceId,
            @Parameter(name = "Client-Id", description = "客户端标识", required = false, in = ParameterIn.HEADER) @RequestHeader(value = "Client-Id", required = false) String clientId) {
        if (productControl || speakerControl) return ResponseEntity.status(403).header("Cache-Control","no-store").body("Product device control required");
        if (StringUtils.isBlank(deviceId)) {
            return ResponseEntity.status(202).build();
        }
        DeviceEntity device = deviceService.getDeviceByMacAddress(deviceId);
        if (device == null) {
            return ResponseEntity.status(202).build();
        }
        return ResponseEntity.ok("success");
    }

    @GetMapping
    @Hidden
    public ResponseEntity<String> getOTA() {
        String mqttUdpConfig = sysParamsService.getValue(Constant.SERVER_MQTT_GATEWAY, false);
        if (StringUtils.isBlank(mqttUdpConfig)) {
            return ResponseEntity.ok("OTA接口不正常，缺少mqtt_gateway地址，请登录智控台，在参数管理找到【server.mqtt_gateway】配置");
        }
        String wsUrl = sysParamsService.getValue(Constant.SERVER_WEBSOCKET, true);
        if (StringUtils.isBlank(wsUrl) || wsUrl.equals("null")) {
            return ResponseEntity.ok("OTA接口不正常，缺少websocket地址，请登录智控台，在参数管理找到【server.websocket】配置");
        }
        String otaUrl = sysParamsService.getValue(Constant.SERVER_OTA, true);
        if (StringUtils.isBlank(otaUrl) || otaUrl.equals("null")) {
            return ResponseEntity.ok("OTA接口不正常，缺少ota地址，请登录智控台，在参数管理找到【server.ota】配置");
        }
        return ResponseEntity.ok("OTA接口运行正常，websocket集群数量：" + wsUrl.split(";").length);
    }

    @SneakyThrows
    private ResponseEntity<String> createResponse(DeviceReportRespDTO deviceReportRespDTO) {
        ObjectMapper objectMapper = new ObjectMapper();
        objectMapper.setSerializationInclusion(JsonInclude.Include.NON_NULL);
        String json = objectMapper.writeValueAsString(deviceReportRespDTO);
        byte[] jsonBytes = json.getBytes(StandardCharsets.UTF_8);
        return ResponseEntity
                .ok()
                .contentType(MediaType.APPLICATION_JSON)
                .contentLength(jsonBytes.length)
                .body(json);
    }

    /**
     * 简单判断mac地址是否有效（非严格）
     * 
     * @param macAddress
     * @return
     */
    private boolean isMacAddressValid(String macAddress) {
        if (StringUtils.isBlank(macAddress)) {
            return false;
        }
        // MAC地址通常为12位十六进制数字，可以包含冒号或连字符分隔符
        String macPattern = "^([0-9A-Za-z]{2}[:-]){5}([0-9A-Za-z]{2})$";
        return macAddress.matches(macPattern);
    }
}
