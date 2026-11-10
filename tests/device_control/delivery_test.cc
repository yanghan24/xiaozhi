#include <cassert>
#include <fstream>
#include <sstream>
#include "delivery_model.h"

using device_control::DeliveryStatus;
using device_control::ParseDeliveryCheck;

int main(int argc, char** argv) {
    const std::string pending =
        R"({"version":1,"code":"OK","data":{"device_id":"speaker-a","status":"pending","verified":true,"server_time_ms":1794182400000}})";
    auto replace = [&](const std::string& old, const std::string& next) {
        auto value = pending;
        value.replace(value.find(old), old.size(), next);
        return value;
    };
    assert(ParseDeliveryCheck(pending, "speaker-a") == DeliveryStatus::Pending);
    assert(ParseDeliveryCheck(replace("pending", "delivered"), "speaker-a") ==
           DeliveryStatus::Delivered);
    assert(ParseDeliveryCheck(pending, "speaker-b") == DeliveryStatus::Invalid);
    for (const auto& invalid :
         {replace("true", "false"), replace("true", "1"), replace("pending", "ready"),
          replace("OK", "ERROR"), replace("1794182400000", "9007199254740992"),
          replace("1794182400000", "-1"), replace("1794182400000", "1.5"),
          replace("1794182400000", "1e999"),
          replace("\"version\":1", "\"version\":1,\"version\":1"),
          replace("\"verified\":true", "\"verified\":true,\"verified\":true"),
          replace("speaker-a", "speaker-a\\u0000suffix"), pending + "{}",
          pending + std::string(1, '\0'), std::string(4097, ' ')}) {
        assert(ParseDeliveryCheck(invalid, "speaker-a") == DeliveryStatus::Invalid);
    }
    // Optional input from DeviceDeliveryHttpTest, produced by the real Ubuntu HTTP stack.
    if (argc == 4) {
        for (int i = 1; i <= 2; ++i) {
            std::ifstream input(argv[i]);
            assert(input);
            std::ostringstream body;
            body << input.rdbuf();
            assert(ParseDeliveryCheck(body.str(), argv[3]) ==
                   (i == 1 ? DeliveryStatus::Pending : DeliveryStatus::Delivered));
        }
    }
}
