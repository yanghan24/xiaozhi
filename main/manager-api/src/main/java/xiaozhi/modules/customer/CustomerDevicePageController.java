package xiaozhi.modules.customer;

import java.nio.charset.StandardCharsets;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.util.HtmlUtils;

@RestController
final class CustomerDevicePageController {
    private final CustomerDeviceSessionService sessions;
    CustomerDevicePageController(CustomerDeviceSessionService sessions) { this.sessions=sessions; }
    @GetMapping("/customer/devices/{id}")
    ResponseEntity<String> page(@CookieValue(name="customer_browser",required=false) String cookie,@PathVariable String id) {
        var data=sessions.page(cookie,id);
        String content="<p>设备归属保留。退出只停用本台音箱，不退出其他设备或集市账号。</p>";
        String operation=data.get("operation_id").toString();
        if(!operation.isEmpty()) {
            String execution=sessions.operation(cookie,operation).get("execution").toString();
            String message=switch(execution) {
                case "device_acknowledged" -> "音箱已确认停播";
                case "superseded" -> "已被后续操作替代";
                default -> "退出已保存，等待音箱确认停播";
            };
            content+="<p>"+message+"</p><p><a href=\"/xiaozhi/customer/devices/"+HtmlUtils.htmlEscape(id)+"\">刷新状态</a></p>";
        } else if(Boolean.TRUE.equals(data.get("can_sign_out"))) {
            content+="<form method=\"post\" action=\"/xiaozhi/customer/v1/device-sessions/"+HtmlUtils.htmlEscape(id)+"/sign-out\">"
                    +"<input type=\"hidden\" name=\"csrf\" value=\""+HtmlUtils.htmlEscape(data.get("csrf").toString())+"\">"
                    +"<input type=\"hidden\" name=\"idempotency_key\" value=\""+HtmlUtils.htmlEscape(id)+"\"><button type=\"submit\">退出本台音箱</button></form>";
        } else content+="<p>设备状态已变化，请重新扫码。</p>";
        return html(200,"账号与绑定",content);
    }
    @ExceptionHandler(CustomerFailure.class)
    ResponseEntity<String> rejected(CustomerFailure failure) {
        return html(failure.status,"设备页面不可用","<p>身份或设备状态已变化，请使用原归属账号重新扫码。</p>");
    }
    private ResponseEntity<String> html(int status,String title,String content) {
        return ResponseEntity.status(status).contentType(new MediaType("text","html",StandardCharsets.UTF_8))
                .header("Cache-Control","no-store").header("Referrer-Policy","same-origin")
                .header("Content-Security-Policy","default-src 'none'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'")
                .body("<!doctype html><html lang=\"zh-CN\"><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>"+title+"</title><h1>"+title+"</h1>"+content+"</html>");
    }
}