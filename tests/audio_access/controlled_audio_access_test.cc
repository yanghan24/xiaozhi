#include "controlled_audio_access.h"

#include <cassert>

int main() {
    ControlledAudioAccess access;
    assert(!access.IsCurrent(0));
    access.Grant(7);
    assert(access.TryBeginOutput(7));
    access.Revoke(8);
    assert(!access.IsStopped());  // A popped PCM block is still in the driver.
    assert(!access.TryBeginOutput(7));
    access.EndOutput();
    assert(access.IsStopped());

    access.Grant(9);
    assert(!access.TryBeginOutput(7));  // Old work cannot return after re-login.
    assert(access.TryBeginOutput(9));
    access.EndOutput();
    assert(access.IsStopped());
}
