#include "processing_audio_buffer.h"

#include <cassert>
#include <vector>

int main() {
    ProcessingAudioBuffer buffer;
    const auto old_run = buffer.Start(101);
    const int16_t before_logout[] = {11, 12};
    buffer.Append(old_run, before_logout, 2);
    buffer.Stop();
    const auto new_run = buffer.Start(202);
    const int16_t late_fetch[] = {13, 14, 15, 16};
    buffer.Append(old_run, late_fetch, 4);
    const int16_t after_login[] = {21, 22, 23, 24};
    buffer.Append(new_run, after_login, 4);
    std::vector<int16_t> frame;
    assert(buffer.PopFrame(new_run, 4, frame));
    assert((frame == std::vector<int16_t>{21, 22, 23, 24}));
    assert(!buffer.PopFrame(old_run, 4, frame));
    assert(!buffer.PopFrame(new_run, 4, frame));

    // A frame detached for an unlocked callback keeps its original permission
    // even if Stop/Start happens while that callback is blocked by a full queue.
    buffer.Append(new_run, after_login, 4);
    uint64_t source_generation = 0;
    assert(buffer.PopFrame(new_run, 4, frame, source_generation));
    buffer.Stop();
    buffer.Start(303);
    assert(source_generation == 202);
    assert(buffer.GetSourceGeneration() == 303);
}
