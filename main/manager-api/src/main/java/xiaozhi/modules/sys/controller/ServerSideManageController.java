package xiaozhi.modules.sys.controller;

import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.socket.WebSocketHttpHeaders;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.validation.Valid;
import lombok.AllArgsConstructor;
import xiaozhi.common.annotation.LogOperation;
import xiaozhi.common.constant.Constant;
import xiaozhi.common.exception.RenException;
import xiaozhi.common.utils.Result;
import xiaozhi.modules.sys.dto.EmitSeverActionDTO;
import xiaozhi.modules.sys.dto.ServerActionPayloadDTO;
import xiaozhi.modules.sys.service.SysParamsService;
import xiaozhi.modules.sys.service.VoiceServiceEndpoints;
import xiaozhi.modules.sys.utils.WebSocketClientManager;

@RestController
@RequestMapping("/admin/server")
@AllArgsConstructor
public class ServerSideManageController {
    private final SysParamsService parameters;
    private final VoiceServiceEndpoints endpoints;
    private static final ObjectMapper JSON=new ObjectMapper();

    @GetMapping("/server-list")
    @RequiresPermissions("sys:role:superAdmin")
    public Result<List<String>> getWsServerList() {
        return new Result<List<String>>().ok(endpoints.addresses());
    }
    @GetMapping("/services")
    @RequiresPermissions("sys:role:superAdmin")
    public Result<List<Map<String,String>>> services() {
        return new Result<List<Map<String,String>>>().ok(endpoints.services());
    }
    @PostMapping("/emit-action")
    @LogOperation("通知语音服务执行管理操作")
    @RequiresPermissions("sys:role:superAdmin")
    public Result<Boolean> emitServerAction(@RequestBody @Valid EmitSeverActionDTO request) {
        if(request.getAction()==null) throw new RenException("不支持的服务管理操作");
        String target;
        try { target=endpoints.managementUrl(request.getTargetWs()); }
        catch(xiaozhi.modules.speaker.SpeakerFailure invalid) { throw new RenException("目标语音服务未配置或地址无效，请检查系统参数"); }
        String secret=parameters.getValue(Constant.SERVER_SECRET,true);
        if(secret==null || secret.isBlank() || "null".equalsIgnoreCase(secret)) throw new RenException("尚未配置服务间认证");
        var headers=new WebSocketHttpHeaders();headers.add("Authorization","Bearer "+secret);
        AtomicBoolean accepted=new AtomicBoolean(false);
        try(var client=new WebSocketClientManager.Builder().connectTimeout(3,TimeUnit.SECONDS)
                .maxSessionDuration(8,TimeUnit.SECONDS).uri(target).headers(headers).build()) {
            client.sendJson(ServerActionPayloadDTO.build(request.getAction(),Map.of()));
            client.listener(text->{
                try {
                    var response=JSON.readTree(text);
                    accepted.set("server".equals(response.path("type").asText())
                            && "success".equals(response.path("status").asText())
                            && request.getAction().getValue().equals(response.path("content").path("action").asText()));
                } catch(Exception invalid) { accepted.set(false); }
                return true; // One response, including explicit errors; never retry a restart.
            });
        } catch(InterruptedException interrupted) {
            Thread.currentThread().interrupt();throw new RenException("管理操作等待已中断");
        } catch(Exception unavailable) { throw new RenException("无法连接语音服务或未收到操作确认"); }
        if(!accepted.get()) throw new RenException("语音服务未确认本次操作，请检查配置或服务日志");
        return new Result<Boolean>().ok(true);
    }
}
