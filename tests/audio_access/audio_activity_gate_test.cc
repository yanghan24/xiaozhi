#include "audio_activity_gate.h"

#include <cassert>
#include <future>
#include <thread>

int main() {
    AudioActivityGate gate;
    std::promise<void> permission_checked, resume_allowed;
    auto resume = resume_allowed.get_future();
    bool recording = false;
    std::thread recorder([&] {
        auto start = gate.TryBegin();
        assert(start);
        permission_checked.set_value();
        resume.wait();
        recording = true;
    });
    permission_checked.get_future().wait();
    // Resume has passed permission but has not published Recording yet.
    // OTA must not freeze this apparently idle device midway through startup.
    assert(!gate.TryReserve());
    resume_allowed.set_value();
    recorder.join();
    assert(recording);

    recording = false;
    assert(gate.TryReserve());
    std::thread late_start([&] {
        auto start = gate.TryBegin();
        assert(!start);  // Recording, music and voice share this admission gate.
    });
    late_start.join();
    assert(!gate.TryReserve());
    gate.Release();
    {
        auto output = gate.TryBegin();
        assert(output);
        // A driver write remains busy even after the last block left its queue.
        assert(!gate.TryReserve());
    }
    assert(gate.TryReserve());
    gate.Release();
    assert(gate.TryBegin());
}
