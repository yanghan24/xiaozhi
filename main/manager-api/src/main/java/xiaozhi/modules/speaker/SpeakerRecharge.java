package xiaozhi.modules.speaker;

import java.time.Duration;
import java.util.Map;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.stereotype.Service;
import static xiaozhi.modules.speaker.SpeakerSupport.number;

/** Device-facing checkout. Market owns the price, order and settlement. */
@Service
@ConditionalOnProperty(name={"speaker.enabled","speaker.recharge-enabled"},havingValue="true")
class SpeakerRecharge {
    record Entry(String entry_id,String url,long expires_in,long poll_interval) {
        @Override public String toString() { return "RechargeEntry[redacted]"; }
    }
    record Status(String status,String balance) {}
    private record Owner(String deviceId,String customerId,long binding,long permission) {
        String cacheValue() { return deviceId + "|" + customerId + "|" + binding + "|" + permission; }
    }

    private final SpeakerDevices devices;
    private final SpeakerOperations operations;
    private final SpeakerGrants grants;
    private final SpeakerLogins logins;
    private final SpeakerMarket market;
    private final StringRedisTemplate redis;

    SpeakerRecharge(SpeakerDevices devices,SpeakerOperations operations,SpeakerGrants grants,SpeakerLogins logins,
                    SpeakerMarket market,StringRedisTemplate redis) {
        this.devices=devices;
        this.operations=operations;
        this.grants=grants;
        this.logins=logins;
        this.market=market;
        this.redis=redis;
    }

    Map<String,Object> authorize(SpeakerDevices.Identity identity) {
        owner(identity);
        return logins.beginRecharge(identity);
    }

    Entry open(SpeakerDevices.Identity identity) {
        var owner=owner(identity);
        var entry=market.rechargeEntry(tokens(owner));
        ensureOwner(identity,owner);
        redis.opsForValue().set(key(entry.entryId()),owner.cacheValue(),Duration.ofHours(24));
        return new Entry(entry.entryId(),entry.url(),entry.expiresIn(),entry.pollInterval());
    }

    Status status(SpeakerDevices.Identity identity,String entryId) {
        if (!SpeakerMarket.validEntryId(entryId)) throw new SpeakerFailure(400,"INVALID_RECHARGE_ENTRY");
        var owner=owner(identity);
        if (!owner.cacheValue().equals(redis.opsForValue().get(key(entryId)))) {
            throw new SpeakerFailure(404,"RECHARGE_ENTRY_NOT_FOUND");
        }
        String status=market.rechargeStatus(entryId,tokens(owner));
        ensureOwner(identity,owner);
        String balance=null;
        if ("paid".equals(status)) {
            if (Boolean.TRUE.equals(redis.opsForValue().setIfAbsent(key(entryId)+":balance","1",Duration.ofSeconds(15)))) {
                grants.refreshBalance(owner.customerId());
            }
            balance=grants.deviceBalance(owner.customerId());
            ensureOwner(identity,owner);
        }
        return new Status(status,balance);
    }

    private SpeakerMarket.Tokens tokens(Owner owner) {
        var grant=grants.grant(owner.customerId());
        if (grant==null || !SpeakerMarket.allowsRecharge(java.util.Objects.toString(grant.get("token_scope"),""))) {
            throw new SpeakerFailure(409,"RECHARGE_AUTHORIZATION_REQUIRED");
        }
        var tokens=grants.currentTokens(owner.customerId());
        if (!tokens.allowsRecharge()) throw new SpeakerFailure(409,"RECHARGE_AUTHORIZATION_REQUIRED");
        return tokens;
    }

    private Owner owner(SpeakerDevices.Identity identity) {
        var device=devices.load(identity,false);
        if (!"authorized".equals(operations.useStatus(device))) throw new SpeakerFailure(403,"DEVICE_RECHARGE_UNAVAILABLE");
        return new Owner(identity.id(),device.get("customer_id").toString(),number(device,"binding_version"),number(device,"permission_version"));
    }

    private void ensureOwner(SpeakerDevices.Identity identity,Owner expected) {
        if (!expected.equals(owner(identity))) throw new SpeakerFailure(409,"DEVICE_CHANGED");
    }

    private static String key(String id) { return "speaker:recharge:"+id; }
}
