#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

struct OtaImageTarget {
    std::string artifact_sha256;
    std::string target_elf_sha256;
    size_t image_size = 0;
};

// HTTP bytes and ESP partition operations are the platform boundary. The
// installer owns ordering, exact-length validation and activation decisions.
class OtaImageIO {
public:
    virtual ~OtaImageIO() = default;
    virtual size_t Length() const = 0;
    virtual size_t Capacity() const = 0;
    virtual int Read(uint8_t* bytes, size_t capacity) = 0;
    virtual bool Hash(const uint8_t* bytes, size_t length) = 0;
    virtual std::string Digest() = 0;
    virtual bool CanStart() const = 0;
    virtual bool Connected() const = 0;
    virtual bool Begin(size_t length) = 0;
    virtual bool Write(const uint8_t* bytes, size_t length) = 0;
    virtual bool End() = 0;
    virtual bool Activate() = 0;
    virtual void Abort() = 0;
    virtual void Progress(size_t received, size_t length) = 0;
};
bool InstallOtaImage(OtaImageIO& io, const OtaImageTarget* expected);
