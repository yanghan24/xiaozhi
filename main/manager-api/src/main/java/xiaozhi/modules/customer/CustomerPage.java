package xiaozhi.modules.customer;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Base64;
import org.springframework.web.util.HtmlUtils;

/** Shared phone-page presentation. Callers escape dynamic values in trusted HTML fragments. */
final class CustomerPage {
    private CustomerPage() {}

    private static final String CSS = """
        :root{color-scheme:light;font-family:"PingFang SC","HarmonyOS Sans SC","Microsoft YaHei",system-ui,sans-serif;color:#102047;background:#f3f6fc;font-synthesis:none}
        *{box-sizing:border-box}body{margin:0;min-width:280px;min-height:100svh;background:linear-gradient(180deg,#091339 0,#102b79 310px,#f3f6fc 310px)}
        .masthead{max-width:1000px;margin:auto;padding:24px 28px;display:flex;align-items:center;justify-content:space-between;gap:16px;color:#fff}
        .brand{display:flex;align-items:center;gap:10px;font-size:17px;font-weight:700;letter-spacing:.02em}.mark{width:30px;height:30px;display:grid;place-items:center;background:#2347cf;border:1px solid #5d7cf0;border-radius:9px;color:#fff;font-size:17px}
        .service{font-size:12px;color:#c3d1f6;border-left:1px solid #425483;padding-left:14px;white-space:nowrap}
        main{width:min(100% - 36px,460px);margin:18px auto 0}.intro{color:#fff;padding:0 6px 27px}.eyebrow{color:#73d9f4;font-size:12px;letter-spacing:.16em;margin:0 0 11px}.intro-title{font-size:29px;line-height:1.4;letter-spacing:.01em;font-weight:650;margin:0}.intro-title span{color:#53cef2}.intro-copy{font-size:13px;line-height:1.8;color:#c6d4f5;margin:10px 0 0}
        .card{background:#fff;border:1px solid #e2e8f3;border-radius:22px;padding:30px 28px;box-shadow:0 16px 45px #071b4a0d;overflow-wrap:anywhere}.section-label{display:flex;align-items:center;gap:7px;font-size:11px;letter-spacing:.12em;color:#617393;margin:0 0 17px}.dot{width:6px;height:6px;background:#2454da;border-radius:50%}h1{font-size:23px;line-height:1.5;letter-spacing:-.02em;margin:0 0 18px;font-weight:650;color:#102047}
        p{font-size:14px;line-height:1.85;color:#5c6b85;margin:13px 0}.device{padding:14px 16px;background:#f4f7fd;border:1px solid #e4ebf8;border-radius:12px;margin:0 0 19px;color:#233b6b;font-weight:600}.device small{display:block;color:#72819b;font-size:11px;font-weight:400;letter-spacing:.06em;margin-bottom:4px}
        form{margin:23px 0 0}button,.card a{font:inherit;font-size:14px;font-weight:600;display:flex;width:100%;min-height:50px;align-items:center;justify-content:center;padding:13px 16px;border-radius:12px;text-decoration:none;line-height:1.6;text-align:center;cursor:pointer;touch-action:manipulation}button{border:1px solid #1740bf;background:#1232a6;color:#fff;box-shadow:0 5px 12px #1232a61a}button:hover{background:#0d298e}button:active{transform:translateY(1px)}.card a{color:#1232a6;border:1px solid #dce5f7;background:#f5f8ff}.card a:hover{background:#eaf0ff}button:focus-visible,a:focus-visible{outline:3px solid #19a6d2;outline-offset:4px}
        .footnote{margin:21px 0 0;padding-top:18px;border-top:1px solid #edf0f6;font-size:12px;color:#7a879d;line-height:1.8}.footer{text-align:center;color:#7c89a0;font-size:11px;line-height:1.9;padding:24px 15px 28px}.footer strong{color:#596b89;font-weight:500;display:block}.danger{background:#fff;color:#a33636;border-color:#e7c9c9;box-shadow:none}.danger:hover{background:#fff4f3}
        @media(max-width:360px){.masthead{padding:20px 18px}.brand{font-size:15px}.service{font-size:11px;padding-left:10px}.card{padding:25px 22px}.intro-title{font-size:26px}h1{font-size:21px}}
        @media(min-width:700px){body{background:linear-gradient(180deg,#091339 0,#102b79 350px,#f3f6fc 350px)}main{margin-top:35px}.intro{padding-bottom:30px}.intro-title{font-size:32px}.card{padding:32px}}
        """;

    static String policy(String formSources) {
        return "default-src 'none'; style-src 'sha256-" + styleHash() + "'; form-action " + formSources
                + "; frame-ancestors 'none'; base-uri 'none'";
    }

    private static String styleHash() {
        try {
            return Base64.getEncoder().encodeToString(MessageDigest.getInstance("SHA-256")
                    .digest(CSS.getBytes(StandardCharsets.UTF_8)));
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    static String render(String title, String content) {
        String escaped = HtmlUtils.htmlEscape(title);
        return "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\">"
                + "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"color-scheme\" content=\"light\">"
                + "<title>" + escaped + " · 小智音箱</title><style>" + CSS + "</style></head><body>"
                + "<header class=\"masthead\"><div class=\"brand\"><span class=\"mark\" aria-hidden=\"true\">◈</span>元筑AI集市</div>"
                + "<span class=\"service\">小智设备服务</span></header><main>"
                + "<div class=\"intro\"><p class=\"eyebrow\">XIAOZHI · AI MARKET</p>"
                + "<p class=\"intro-title\">连接你的音箱<br><span>开启智能对话</span></p>"
                + "<p class=\"intro-copy\">通过集市账号，管理你的音箱与使用授权。</p></div>"
                + "<section class=\"card\" aria-labelledby=\"page-title\"><div class=\"section-label\"><span class=\"dot\"></span>账号与设备</div>"
                + "<h1 id=\"page-title\">" + escaped + "</h1>" + content
                + "<p class=\"footnote\">身份确认由 AI 集市完成，请勿向他人提供登录凭据。</p></section>"
                + "<footer class=\"footer\"><strong>元筑AI集市 · 小智智能音箱</strong>让智能服务，融入每一次对话</footer></main></body></html>";
    }
}
