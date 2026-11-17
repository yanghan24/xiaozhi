#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "ota_image_writer.h"

namespace {
void Check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Flash : OtaImageIO {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(4096, 0);
    size_t read = 0, written = 0, content_length = 4096;
    bool activated = false, begun = false, ended = false, aborted = false;
    bool start_allowed = true, connection = true, write_ok = true, end_ok = true;
    std::string digest = std::string(64, 'd');
    Flash() {
        bytes[0] = 0xe9;
        bytes[32] = 0x32;
        bytes[33] = 0x54;
        bytes[34] = 0xcd;
        bytes[35] = 0xab;
        std::fill(bytes.begin() + 176, bytes.begin() + 208, 0xcc);
    }
    size_t Length() const override { return content_length; }
    size_t Capacity() const override { return 0x3f0000; }
    int Read(uint8_t* target, size_t capacity) override {
        size_t count = std::min({capacity, size_t(73), bytes.size() - read});
        std::memcpy(target, bytes.data() + read, count);
        read += count;
        return count;
    }
    bool Hash(const uint8_t*, size_t) override { return true; }
    std::string Digest() override { return digest; }
    bool CanStart() const override { return start_allowed; }
    bool Connected() const override { return connection; }
    bool Begin(size_t) override {
        begun = true;
        return true;
    }
    bool Write(const uint8_t*, size_t count) override {
        written += count;
        return write_ok;
    }
    bool End() override {
        ended = true;
        return end_ok;
    }
    bool Activate() override {
        activated = true;
        return true;
    }
    void Abort() override { aborted = true; }
    void Progress(size_t, size_t) override {}
};
void VerifyBeforeBoot() {
    OtaImageTarget expected{std::string(64, 'd'), std::string(64, 'c'), 4096};
    Flash bad;
    bad.digest = std::string(64, 'a');
    Check(!InstallOtaImage(bad, &expected) && !bad.activated,
          "wrong downloaded SHA must never select a boot partition");
    Flash good;
    Check(InstallOtaImage(good, &expected) && good.activated && good.ended &&
              good.written == expected.image_size,
          "complete matching image must validate before selecting the next boot partition");
}
void RejectIncompleteOrWrongImage() {
    OtaImageTarget expected{std::string(64, 'd'), std::string(64, 'c'), 4096};
    for (const std::string reason : {"truncated", "extra", "length", "elf", "header", "expiry",
                                     "offline", "write", "image-validation"}) {
        Flash flash;
        if (reason == "truncated")
            flash.bytes.resize(4000);
        if (reason == "extra")
            flash.bytes.resize(4100);
        if (reason == "length")
            flash.content_length = 5000;
        if (reason == "elf")
            flash.bytes[176] = 0;
        if (reason == "header")
            flash.bytes[32] = 0;
        if (reason == "expiry")
            flash.start_allowed = false;
        if (reason == "offline")
            flash.connection = false;
        if (reason == "write")
            flash.write_ok = false;
        if (reason == "image-validation")
            flash.end_ok = false;
        Check(!InstallOtaImage(flash, &expected) && !flash.activated, reason.c_str());
        if (flash.begun)
            Check(flash.aborted, "failed partial installation must abort its OTA handle");
    }
}
}  // namespace
int main() {
    try {
        VerifyBeforeBoot();
        RejectIncompleteOrWrongImage();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "OTA image validation passed\n";
}
