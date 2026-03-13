#pragma once

namespace Bunnyhop {
    void Run();
    void Reset();
    // Called from game thread (Present hook) for no-slowdown
    void RunGameThread();
}
