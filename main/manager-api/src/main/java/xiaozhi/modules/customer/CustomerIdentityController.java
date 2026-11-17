package xiaozhi.modules.customer;

import org.springframework.beans.factory.annotation.Qualifier;

import org.springframework.http.ResponseEntity;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseCookie;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.util.HtmlUtils;
import org.springframework.web.bind.annotation.*;
import java.util.Map;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
public class CustomerIdentityController {
    private static final MediaType HTML_UTF8 = new MediaType("text","html",java.nio.charset.StandardCharsets.UTF_8);
    private final JdbcTemplate db;
    private final java.time.Clock clock;
    private final CustomerIdentityService identities;
    private final CustomerOidcConfig config;
    private final CustomerBindingService bindings;
    private final CustomerDeviceSessionService sessions;
    public CustomerIdentityController(JdbcTemplate db, CustomerIdentityService identities, @Qualifier("customerTime") java.time.Clock clock, CustomerOidcConfig config, CustomerBindingService bindings, CustomerDeviceSessionService sessions) {
        this.db = db; this.identities = identities; this.clock = clock; this.config = config; this.bindings=bindings; this.sessions=sessions;
    }

    @ExceptionHandler(CustomerFailure.class)
    ResponseEntity<Map<String, Object>> failure(CustomerFailure e) {
        return ResponseEntity.status(e.status).header("Cache-Control", "no-store").body(Map.of("error", e.getMessage()));
    }
    @ExceptionHandler(Exception.class)
    ResponseEntity<Map<String, Object>> failure(Exception e) {
        // Never let the management exception logger print token requests or SQL values.
        return ResponseEntity.status(503).header("Cache-Control", "no-store").body(Map.of("error", "IDENTITY_UNAVAILABLE"));
    }

    @PostMapping(value = "/customer/binding-flows", consumes = MediaType.APPLICATION_JSON_VALUE)
    ResponseEntity<Map<String, Object>> createFlow(@CookieValue(name="customer_browser", required=false) String cookie,
            @RequestHeader(name="X-CSRF-Token", required=false) String csrf,
            @RequestHeader(name="Origin", required=false) String origin, @RequestBody Map<String,String> body) {
        return ResponseEntity.status(201).header("Cache-Control", "no-store")
                .body(Map.of("data", identities.createFlow(cookie, csrf, origin, body.get("request_handle"))));
    }
    @PostMapping(value = "/customer/binding-flows", consumes = MediaType.APPLICATION_FORM_URLENCODED_VALUE)
    ResponseEntity<Void> createFlowForm(@CookieValue(name="customer_browser", required=false) String cookie,
            @RequestHeader(name="Origin", required=false) String origin,
            @RequestParam String csrf, @RequestParam String request_handle) {
        var data = identities.createFlow(cookie, csrf, origin, request_handle);
        return ResponseEntity.status(303).header("Cache-Control", "no-store")
                .header("Location", data.get("authorization_url").toString()).build();
    }
    @GetMapping("/customer/binding-flows/{id}")
    ResponseEntity<Map<String, Object>> flowStatus(@CookieValue(name="customer_browser", required=false) String cookie, @PathVariable String id) {
        return ResponseEntity.ok().header("Cache-Control", "no-store").body(Map.of("data", identities.flowStatus(cookie, id)));
    }
    @GetMapping("/customer/oidc/callback")
    ResponseEntity<Void> callback(@CookieValue(name="customer_browser", required=false) String cookie,
            @RequestParam(required=false) String state, @RequestParam(required=false) String code, @RequestParam(required=false) String error) {
        String location;
        try { location = "/xiaozhi/customer/results/" + identities.callback(cookie, state, code, error); }
        catch (CustomerFailure e) { location = "/xiaozhi/customer/identity-error/" + e.status; }
        catch (Exception e) { location = "/xiaozhi/customer/identity-error/503"; }
        return ResponseEntity.status(303).header("Location", location)
                .header("Cache-Control", "no-store").header("Referrer-Policy", "no-referrer").build();
    }
    @GetMapping("/customer/identity-error/{status}")
    ResponseEntity<String> identityError(@PathVariable int status) {
        if (!java.util.Set.of(400,401,409,410,502,503).contains(status)) status = 400;
        return ResponseEntity.status(status).contentType(HTML_UTF8).header("Cache-Control", "no-store")
                .header("Referrer-Policy", "no-referrer").header("Content-Security-Policy", CustomerPage.policy("'self'"))
                .body(CustomerPage.render("身份确认未完成", "<p>流程无效、已使用或已过期，请重新扫码。</p>"));
    }
    @PostMapping(value="/customer/oidc/backchannel-logout", consumes=MediaType.APPLICATION_FORM_URLENCODED_VALUE)
    ResponseEntity<Void> backchannel(@RequestParam(name="logout_token", required=false) String token) {
        identities.backchannelLogout(token);
        return ResponseEntity.noContent().header("Cache-Control", "no-store").build();
    }
    @GetMapping("/customer/results/{id}")
    ResponseEntity<String> result(@CookieValue(name="customer_browser", required=false) String cookie, @PathVariable String id) {
        var data=identities.flowStatus(cookie,id);
        String status = data.get("status").toString();
        String message = switch (status) {
            case "IDENTITY_VERIFIED" -> "身份已确认，等待手机确认";
            case "IDENTITY_REFUSED" -> "已取消身份授权，请重新扫码";
            case "AWAITING_IDENTITY" -> "等待 AI 集市身份确认";
            case "SESSION_ENDED" -> "身份会话已结束，请重新扫码";
            case "FLOW_EXPIRED" -> "身份确认流程已过期，请重新扫码";
            default -> "身份确认未完成，请重新扫码";
        };
        if (!"AVAILABLE".equals(data.get("request_status"))) message="设备请求已过期或已使用，请重新扫码";
        boolean bound="IDENTITY_VERIFIED".equals(status) && bindings.isBound(cookie,id);
        String action="<p>此步骤尚未绑定设备或恢复音箱使用许可。</p>";
        if (bound) {
            message=switch(data.get("purpose").toString()) {case "account" -> "账号身份已确认";case "resume" -> "恢复操作已完成";default -> "绑定成功";};
            action="<p>设备已归属当前集市账号。当前使用状态请以音箱显示为准。</p><p><a href=\"/xiaozhi/customer/devices/"
                    +HtmlUtils.htmlEscape(sessions.sessionForFlow(cookie,id))+"\">账号与绑定</a></p>";
        } else if ("IDENTITY_VERIFIED".equals(status) && "AVAILABLE".equals(data.get("request_status"))
                && java.util.Set.of("bind","resume").contains(data.get("purpose")) && bindings.isProductFlow(cookie,id)) {
            String csrf=identities.browser(cookie).get("csrf").toString();
            boolean resume="resume".equals(data.get("purpose"));
            action=(resume?"<p>仅原归属账号可恢复本台音箱，其他设备不受影响。</p>":"<p>确认将这台设备绑定到本次登录的集市账号。</p>")+"<form method=\"post\" action=\"/xiaozhi/customer/v1/binding-flows/"
                    + HtmlUtils.htmlEscape(id)+"/confirm\"><input type=\"hidden\" name=\"csrf\" value=\""+HtmlUtils.htmlEscape(csrf)
                    +"\"><input type=\"hidden\" name=\"idempotency_key\" value=\""+HtmlUtils.htmlEscape(id)
                    +"\"><button type=\"submit\">"+(resume?"恢复本台使用":"确认绑定设备")+"</button></form>";
        }
        if (!bound && "IDENTITY_VERIFIED".equals(status) && "AVAILABLE".equals(data.get("request_status"))
                && "account".equals(data.get("purpose")) && bindings.isProductFlow(cookie,id)) {
            action="<p>仅原归属账号可管理本台音箱。</p><form method=\"post\" action=\"/xiaozhi/customer/v1/binding-flows/"
                    +HtmlUtils.htmlEscape(id)+"/open-account\"><input type=\"hidden\" name=\"csrf\" value=\""
                    +HtmlUtils.htmlEscape(identities.browser(cookie).get("csrf").toString())+"\"><input type=\"hidden\" name=\"idempotency_key\" value=\""
                    +HtmlUtils.htmlEscape(id)+"\"><button type=\"submit\">进入本台设备页</button></form>";
        }
        return ResponseEntity.ok().contentType(HTML_UTF8).header("Cache-Control", "no-store")
                .header("Referrer-Policy", "same-origin")
                .header("Content-Security-Policy", CustomerPage.policy("'self'"))
                .body(CustomerPage.render(message, "<div class=\"device\"><small>当前设备</small>"
                        +HtmlUtils.htmlEscape(data.get("device_label").toString())+"</div>"+action));
    }

    @GetMapping("/bind/{handle}")
    public ResponseEntity<String> bindingPage(@PathVariable String handle,
            @CookieValue(name="customer_browser", required=false) String existingCookie) {
        Map<String,Object> request;
        try { request=identities.request(handle); }
        catch (CustomerFailure failure) {
            String message = switch (failure.status) {
                case 404 -> "设备请求不存在";
                case 410 -> "设备请求已过期，请重新扫码";
                default -> "设备请求已使用";
            };
            return ResponseEntity.status(failure.status).contentType(HTML_UTF8).header("Cache-Control","no-store")
                    .header("Referrer-Policy","no-referrer").header("Content-Security-Policy",CustomerPage.policy("'self'"))
                    .body(CustomerPage.render(message,"<p>请在音箱上重新打开二维码，再用手机扫码继续。</p>"));
        }
        long now = clock.millis();
        String browser = existingCookie;
        String csrf;
        boolean created = false;
        try { csrf = (String) identities.browser(browser).get("csrf"); }
        catch (CustomerFailure e) {
            browser = CustomerSecrets.random();
            csrf = CustomerSecrets.random();
            created = true;
            db.update("INSERT INTO customer_browser(id_hash,csrf,last_activity_ms) VALUES (?,?,?)", CustomerSecrets.hash(browser), csrf, now);
        }
        String alias = HtmlUtils.htmlEscape(String.valueOf(request.get("alias")));
        var response = ResponseEntity.ok().contentType(HTML_UTF8);
        if (created) response.header("Set-Cookie", ResponseCookie.from("customer_browser", browser).httpOnly(true).secure(true)
                .sameSite("Lax").path("/xiaozhi").build().toString());
        return response.header("Cache-Control", "no-store")
                // no-referrer turns a navigation POST's Origin into null in Chromium.
                // same-origin preserves CSRF validation without leaking the request to the provider.
                .header("Referrer-Policy", "same-origin")
                .header("Content-Security-Policy", CustomerPage.policy("'self' "
                        + (config.issuer == null ? "" : java.net.URI.create(config.issuer).resolve("/").toString())
                        ))
                .body(CustomerPage.render("确认设备身份", "<div class=\"device\"><small>当前设备</small>" + alias + "</div><p>通过 AI 集市确认身份，随后在手机上确认设备操作。</p>"
                        + "<form method=\"post\" action=\"/xiaozhi/customer/v1/binding-flows\">"
                        + "<input type=\"hidden\" name=\"csrf\" value=\"" + csrf + "\">"
                        + "<input type=\"hidden\" name=\"request_handle\" value=\"" + HtmlUtils.htmlEscape(handle) + "\">"
                        + "<button type=\"submit\">通过 AI 集市确认身份</button></form>"));
    }
}
