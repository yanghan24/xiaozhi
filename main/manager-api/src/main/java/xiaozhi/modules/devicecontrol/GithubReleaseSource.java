package xiaozhi.modules.devicecontrol;

import com.fasterxml.jackson.databind.JsonNode;
import org.springframework.stereotype.Component;
import java.net.*;
import java.io.*;
import java.util.Set;
import com.fasterxml.jackson.databind.ObjectMapper;

/** External GitHub boundary; callers only supply project release/asset IDs. */
@Component
public class GithubReleaseSource {
    static final int MAX_IMAGE = 8 * 1024 * 1024;
    private static final String ROOT="https://api.github.com/repos/yanghan24/xiaozhi/releases";
    public JsonNode releases(int page) { return json(ROOT+"?per_page=20&page="+page); }
    public JsonNode release(long id) { return json(ROOT+"/"+id); }
    public byte[] asset(long id, int limit) { return fetch(URI.create(ROOT+"/assets/"+id),limit,true); }
    private JsonNode json(String url) {
        try { return new ObjectMapper().readTree(fetch(URI.create(url),1024*1024,false)); }
        catch(IOException malformed) { throw new ControlFailure(502,"GITHUB_INVALID_RESPONSE"); }
    }
    private byte[] fetch(URI uri,int limit,boolean binary) {
        long deadline=System.nanoTime()+60_000_000_000L;
        try {
            for(int redirect=0;redirect<5;redirect++) {
                if(!"https".equals(uri.getScheme()) || uri.getRawUserInfo()!=null || uri.getRawFragment()!=null
                        || (uri.getPort()!=-1 && uri.getPort()!=443)
                        || !Set.of("api.github.com","release-assets.githubusercontent.com","objects.githubusercontent.com").contains(uri.getHost()))
                    throw new ControlFailure(502,"GITHUB_UNSAFE_REDIRECT");
                for(var address:InetAddress.getAllByName(uri.getHost()))
                    if(address.isAnyLocalAddress() || address.isLoopbackAddress() || address.isLinkLocalAddress()
                            || address.isSiteLocalAddress() || address.isMulticastAddress())
                        throw new ControlFailure(502,"GITHUB_UNSAFE_REDIRECT");
                var connection=(HttpURLConnection)uri.toURL().openConnection();
                connection.setInstanceFollowRedirects(false);
                connection.setConnectTimeout(15000); connection.setReadTimeout(30000);
                connection.setRequestProperty("Accept",binary?"application/octet-stream":"application/vnd.github+json");
                connection.setRequestProperty("User-Agent","xiaozhi-firmware-catalog");
                connection.setRequestProperty("X-GitHub-Api-Version","2022-11-28");
                try {
                    int status=connection.getResponseCode();
                    if(Set.of(301,302,303,307,308).contains(status)) {
                        String location=connection.getHeaderField("Location");
                        if(location==null) throw new IOException();
                        uri=uri.resolve(location); continue;
                    }
                    if(status!=200) throw new ControlFailure(502,status==403 || status==429?"GITHUB_RATE_LIMITED":"GITHUB_UNAVAILABLE");
                    if(connection.getContentLengthLong()>limit) throw new ControlFailure(400,"ASSET_SIZE_INVALID");
                    try(var input=connection.getInputStream();var out=new ByteArrayOutputStream()) {
                        byte[] buffer=new byte[16384];
                        for(int count;(count=input.read(buffer))!=-1;) {
                            if(System.nanoTime()>deadline) throw new IOException();
                            if(out.size()+count>limit) throw new ControlFailure(400,"ASSET_SIZE_INVALID");
                            out.write(buffer,0,count);
                        }
                        return out.toByteArray();
                    }
                } finally { connection.disconnect(); }
            }
        } catch(java.net.SocketTimeoutException timeout) { throw new ControlFailure(502,"GITHUB_TIMEOUT"); }
        catch(IOException | IllegalArgumentException bad) { throw new ControlFailure(502,"GITHUB_UNAVAILABLE"); }
        throw new ControlFailure(502,"GITHUB_UNAVAILABLE");
    }
}
