package xiaozhi.modules.speaker;

import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.http.CacheControl;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestHeader;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/speaker/v1/account/recharge")
@ConditionalOnProperty(name={"speaker.enabled","speaker.recharge-enabled"},havingValue="true")
class SpeakerRechargeController {
    private final SpeakerDevices devices;
    private final SpeakerRecharge recharge;

    SpeakerRechargeController(SpeakerDevices devices,SpeakerRecharge recharge) {
        this.devices=devices;
        this.recharge=recharge;
    }

    @PostMapping("/authorization")
    public ResponseEntity<?> authorize(@RequestHeader(name="Authorization",required=false) String auth,
                                      @RequestHeader(name="Device-Id",required=false) String device) {
        return privateResponse(recharge.authorize(devices.authenticate(auth,device)));
    }

    @PostMapping("/entries")
    public ResponseEntity<?> open(@RequestHeader(name="Authorization",required=false) String auth,
                                 @RequestHeader(name="Device-Id",required=false) String device) {
        return privateResponse(recharge.open(devices.authenticate(auth,device)));
    }

    @GetMapping("/entries/{id}")
    public ResponseEntity<?> status(@RequestHeader(name="Authorization",required=false) String auth,
                                   @RequestHeader(name="Device-Id",required=false) String device,@PathVariable String id) {
        return privateResponse(recharge.status(devices.authenticate(auth,device),id));
    }

    private static ResponseEntity<?> privateResponse(Object body) {
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).body(body);
    }
}
