package xiaozhi.modules.model.market;

import static org.junit.jupiter.api.Assertions.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
import com.sun.net.httpserver.HttpServer;
import org.junit.jupiter.api.*;
import org.junit.jupiter.api.io.TempDir;
import xiaozhi.modules.speaker.SpeakerFailure;

class MarketModelCatalogTest {
    @TempDir Path dir;
    HttpServer server;MarketModelCatalog catalog;
    AtomicInteger tokens=new AtomicInteger(),pages=new AtomicInteger();
    boolean rejectFirst,forbidden;
    @BeforeEach void start() throws Exception {
        server=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
        String issuer="http://127.0.0.1:"+server.getAddress().getPort()+"/oidc";
        Path secret=dir.resolve("fixture-secret");Files.writeString(secret,"backend-fixture-secret");
        catalog=new MarketModelCatalog(new MarketCatalogSettings(issuer,"backend-client",secret.toString()));
        server.createContext("/",e -> {
            String path=e.getRequestURI().getPath(),body;int status=200;
            if(path.endsWith("openid-configuration")) body="{\"issuer\":\""+issuer+"\",\"token_endpoint\":\""+issuer+"/token\"}";
            else if(path.equals("/oidc/token")) {
                assertEquals("Basic "+Base64.getEncoder().encodeToString("backend-client:backend-fixture-secret".getBytes(StandardCharsets.UTF_8)),e.getRequestHeaders().getFirst("Authorization"));
                String form=new String(e.getRequestBody().readAllBytes(),StandardCharsets.UTF_8);
                assertEquals("grant_type=client_credentials&scope=models%3Aread",form);
                int n=tokens.incrementAndGet();
                body="{\"access_token\":\"application-fixture-"+n+"\",\"scope\":\"models:read\",\"token_type\":\"Bearer\",\"expires_in\":300}";
            } else {
                assertTrue(e.getRequestHeaders().getFirst("Authorization").startsWith("Bearer application-fixture-"));
                int n=pages.incrementAndGet();
                if(forbidden) {status=403;body="{}";}
                else if(rejectFirst && n==1) {status=401;body="{}";}
                else {
                    int index=e.getRequestURI().getQuery().contains("page=2&")?2:1;
                    body="{\"code\":20000,\"data\":{\"items\":[{\"publicId\":\"model-"+index+"\",\"name\":\"模型\",\"protocol\":\"openai\",\"api_key\":\"must-not-leak\",\"upstream_url\":\"private\"}],\"total\":2,\"totalPages\":2}}";
                }
            }
            byte[] bytes=body.getBytes(StandardCharsets.UTF_8);e.sendResponseHeaders(status,bytes.length);
            try(var out=e.getResponseBody()) {out.write(bytes);}
        });
        server.start();
    }
    @AfterEach void stop() {server.stop(0);}
    @Test void cachesOnlyApplicationTokenAndReadsAllDirectoryPagesWithoutLeakingFields() {
        var first=catalog.list();var second=catalog.list();
        assertEquals(2,first.size());assertEquals(2,second.size());
        assertEquals(1,tokens.get());assertEquals(4,pages.get());
        assertFalse(first.toString().contains("must-not-leak"));assertFalse(first.toString().contains("private"));
    }
    @Test void renewsOn401OnceAndDoesNotBorrowUserKeys() {
        rejectFirst=true;
        assertEquals(2,catalog.list().size());assertEquals(2,tokens.get());assertEquals(3,pages.get());
    }
    @Test void refusesPermissionFailuresWithoutRetryOrEmptySuccess() {
        forbidden=true;
        assertEquals("MARKET_CATALOG_FORBIDDEN",assertThrows(SpeakerFailure.class,()->catalog.list()).getMessage());
        assertEquals(1,tokens.get());assertEquals(1,pages.get());
    }
}
