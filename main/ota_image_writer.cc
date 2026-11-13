#include "ota_image_writer.h"
#include <algorithm>
#include <array>
#include <memory>

namespace {
bool HexDigest(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string Hex(const uint8_t* bytes, size_t size) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < size; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}
}  // namespace
bool InstallOtaImage(OtaImageIO& io, const OtaImageTarget* expected) {
    constexpr size_t Page = 4096, Header = 24 + 8 + 256, ElfOffset = 24 + 8 + 144;
    const auto length = io.Length();
    if (length < Header || length > io.Capacity() ||
        (expected && (expected->image_size != length || !HexDigest(expected->artifact_sha256) ||
                      !HexDigest(expected->target_elf_sha256))))
        return false;
    auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[Page]);
    if (!buffer)
        return false;
    size_t buffered = 0, received = 0;
    bool begun = false;
    auto abort = [&]() {
        if (begun)
            io.Abort();
        return false;
    };
    while (true) {
        if (!io.Connected())
            return abort();
        auto count = io.Read(buffer.get() + buffered, Page - buffered);
        if (count < 0 || static_cast<size_t>(count) > Page - buffered ||
            static_cast<size_t>(count) > length - received)
            return abort();
        if (count > 0 && !io.Hash(buffer.get() + buffered, count))
            return abort();
        received += count;
        buffered += count;
        if (!begun && buffered >= Header) {
            // esp_image_header + first segment header + esp_app_desc. The
            // platform adapter statically verifies these SDK layout offsets.
            if (buffer[0] != 0xe9 || buffer[32] != 0x32 || buffer[33] != 0x54 ||
                buffer[34] != 0xcd || buffer[35] != 0xab ||
                (expected && Hex(buffer.get() + ElfOffset, 32) != expected->target_elf_sha256) ||
                !io.CanStart() || !io.Begin(length))
                return false;
            begun = true;
        }
        if (buffered == Page || (count == 0 && buffered > 0)) {
            if (!begun || !io.Write(buffer.get(), buffered))
                return abort();
            buffered = 0;
        }
        io.Progress(received, length);
        if (count == 0)
            break;
    }
    if (!begun || received != length || (expected && io.Digest() != expected->artifact_sha256))
        return abort();
    if (!io.End())
        return abort();
    begun = false;
    return io.Activate();
}
