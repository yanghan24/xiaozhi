#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "maintenance_model.h"

namespace {
const std::string Boot(32, 'a'), Operation(32, 'b'), Elf(64, 'c'), Sha(64, 'd');
const std::string Origin = "https://speaker.example:8443";
const std::string P4SystemInfo =
    R"({"chip_model_name":"esp32p4","chip_info":{"model":18,"cores":2,"revision":302,"features":0},"flash_size":16777216,"application":{"name":"xiaozhi","version":"2.2.6","elf_sha256":"f242b5b68cc3c565335c7eb0b048156209531ebd6fb5e9c54bdf809b9cc58d6e"},"partition_table":[{"label":"nvs","type":1,"subtype":2,"address":36864,"size":16384},{"label":"otadata","type":1,"subtype":0,"address":53248,"size":8192},{"label":"phy_init","type":1,"subtype":1,"address":61440,"size":4096},{"label":"ota_0","type":0,"subtype":16,"address":131072,"size":4128768},{"label":"ota_1","type":0,"subtype":17,"address":4259840,"size":4128768},{"label":"assets","type":1,"subtype":130,"address":8388608,"size":8388608}],"ota":{"label":"ota_0","state":"valid"},"board":{"type":"waveshare-esp32-p4-wifi6-touch-lcd-4.3"}})";
void Check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
std::string Offer() {
    return R"({"version":1,"code":"OK","data":{"server_time_ms":1000000,"operation_ack":null,"command":{"operation_id":")" +
           Operation + R"(","boot_id":")" + Boot +
           R"(","url":"https://speaker.example:8443/xiaozhi/ota/firmware/test?signature=secret","artifact_sha256":")" +
           Sha + R"(","target_elf_sha256":")" + Elf +
           R"(","image_size":4096,"expires_at_ms":1060000,"expires_in_ms":60000}}})";
}
std::string Ack(const std::string& status) {
    return R"({"version":1,"code":"OK","data":{"server_time_ms":1000000,"command":null,"operation_ack":{"operation_id":")" +
           Operation + R"(","status":")" + status +
           R"(","expires_in_ms":50000,"expires_at_ms":1050000}}})";
}
void ConfirmBeforeFlash() {
    std::string persisted;
    int writes = 0;
    device_control::Maintenance client(
        Boot, Origin, "", std::string(64, 'e'), true,
        [&](const std::string& value) {
            persisted = value;
            return true;
        },
        [] { return true; }, [] {},
        [&](const device_control::MaintenanceCommand& command) {
            Check(!persisted.empty(), "operation must survive reboot before flash starts");
            Check(command.artifact_sha256 == Sha && command.image_size == 4096,
                  "flash must receive authenticated target integrity constraints");
            ++writes;
        });
    Check(client.Request(true, "{}", 1000).find("ota_capable") != std::string::npos,
          "connected device advertises OTA without a voice session");
    client.Apply(Offer(), 1100, 100);
    Check(writes == 0 && !persisted.empty(), "offer alone must not write flash");
    Check(client.Request(true, "{}", 1200).find("accepted") != std::string::npos,
          "accepted operation must be reported before execution");
    client.Apply(Ack("accepted"), 1300, 100);
    client.Apply(Ack("accepted"), 1400, 100);
    Check(writes == 1, "matching acknowledgement permits exactly one flash attempt");
}
void RecoveryDoesNotReflash() {
    std::string persisted;
    int writes = 0;
    auto save = [&](const std::string& value) {
        persisted = value;
        return true;
    };
    auto execute = [&](const device_control::MaintenanceCommand&) { ++writes; };
    {
        device_control::Maintenance before(
            Boot, Origin, "", std::string(64, 'e'), true, save, [] { return true; }, [] {},
            execute);
        before.Request(true, "{}", 1000);
        before.Apply(Offer(), 1100, 100);
    }
    const auto accepted = persisted;
    device_control::Maintenance interrupted(
        std::string(32, 'f'), Origin, accepted, std::string(64, 'e'), true, save,
        [] { return true; }, [] {}, execute);
    auto report = interrupted.Request(true, "{}", 1000);
    Check(report.find("interrupted") != std::string::npos &&
              report.find("\"origin_boot_id\":\"" + Boot) != std::string::npos,
          "restart must correlate interruption to original boot without retrying flash");
    interrupted.Apply(Ack("accepted"), 1200, 100);
    Check(writes == 0, "stale accepted ACK after restart must never run flash");
    device_control::Maintenance installed(
        std::string(32, 'f'), Origin, accepted, Elf, true, save, [] { return true; }, [] {},
        execute);
    Check(installed.Request(true, "{}", 1000).find("installed") != std::string::npos,
          "matching running ELF and VALID slot report installed on the new boot");
    installed.Apply(Ack("installed"), 1200, 100);
    Check(installed.Request(true, "{}", 1300).find("\"operation\":null") != std::string::npos,
          "matching terminal ACK retires the pending report");
    device_control::Maintenance unverified(
        std::string(32, 'f'), Origin, accepted, Elf, false, save, [] { return true; }, [] {},
        execute);
    Check(unverified.Request(true, "{}", 1000).find("interrupted") != std::string::npos,
          "matching ELF without VALID must not report successful installation");
}
void UnsafeOffersNeverExecute() {
    for (const std::string reason :
         {"busy", "storage", "expiry", "disconnect", "wrong-boot", "foreign-url"}) {
        int writes = 0, releases = 0;
        device_control::Maintenance client(
            Boot, Origin, "", std::string(64, 'e'), true,
            [&](const std::string&) { return reason != "storage"; },
            [&] { return reason != "busy"; }, [&] { ++releases; },
            [&](const device_control::MaintenanceCommand&) { ++writes; });
        client.Request(true, "{}", 1000);
        auto offer = Offer();
        if (reason == "wrong-boot")
            offer.replace(offer.find(Boot), Boot.size(), std::string(32, 'f'));
        if (reason == "foreign-url")
            offer.replace(offer.find("speaker.example"), 15, "another.example");
        client.Apply(offer, 1100, 100);
        if (reason == "disconnect")
            client.Disconnected();
        client.Apply(Ack("accepted"), reason == "expiry" ? 70000 : 1300, 100);
        Check(writes == 0, reason.c_str());
        if (reason == "expiry" || reason == "disconnect" || reason == "storage")
            Check(releases == 1, "failed or interrupted acceptance must release the idle gate");
    }
}
}  // namespace
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--accept-response") {
        std::ifstream input(argv[2]);
        std::string response((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
        device_control::Maintenance client(
            Boot, argv[3], "", std::string(64, 'e'), true, [](const std::string&) { return true; },
            [] { return true; }, [] {}, [](const device_control::MaintenanceCommand&) {});
        client.Request(true, P4SystemInfo, 1000);
        client.Apply(response, 1100, 100);
        auto accepted = client.Request(true, P4SystemInfo, 1200);
        if (accepted.find("\"status\":\"accepted\"") == std::string::npos) {
            std::cerr << "server offer was rejected by firmware protocol\n";
            return 1;
        }
        std::cout << accepted << '\n';
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--fixture") {
        device_control::Maintenance client(
            Boot, Origin, "", std::string(64, 'e'), true, [](const std::string&) { return true; },
            [] { return true; }, [] {}, [](const device_control::MaintenanceCommand&) {});
        std::cout << client.Request(true, P4SystemInfo, 1000) << '\n';
        return 0;
    }
    try {
        ConfirmBeforeFlash();
        RecoveryDoesNotReflash();
        UnsafeOffersNeverExecute();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "maintenance protocol passed\n";
}
