#include "bhop.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include <buttons.hpp>
#include <windows.h>
#include <cstdint>

namespace Bunnyhop {

    static constexpr uint32_t BTN_PRESSED  = 65537u; // 0x10001
    static constexpr uint32_t BTN_RELEASED = 256u;   // 0x100
    static constexpr ptrdiff_t OFF_FLAGS   = 0x400;

    // CS2 button system: the engine only registers a NEW jump on the
    // TRANSITION from RELEASED -> PRESSED.
    // If we hold PRESSED every iteration while on ground, the game sees
    // only 1 jump total (no transition = no new press).
    //
    // Solution: track last written state. On landing:
    //   1. Write RELEASED first (force transition)
    //   2. Next iteration write PRESSED (game sees fresh press)
    // In air: always RELEASED.

    static bool s_wasOnGround    = false;
    static bool s_needsRelease   = false; // true = must write RELEASED before next PRESSED

    static uintptr_t GetClientBase() {
        return reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
    }

    static uintptr_t GetLocalPawn(uintptr_t clientBase) {
        if (!clientBase) return 0;
        uintptr_t pawn = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, pawn)
            || !Memory::IsValidPtr(pawn))
            return 0;
        return pawn;
    }

    void Run() {
        uintptr_t clientBase = GetClientBase();

        if (!Hooks::g_bBhopEnabled.load() || !(GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            if (clientBase)
                Memory::SafeWrite<uint32_t>(clientBase + cs2_dumper::buttons::jump, BTN_RELEASED);
            s_wasOnGround  = false;
            s_needsRelease = false;
            return;
        }

        __try {
            if (!clientBase) return;

            uintptr_t localPawn = GetLocalPawn(clientBase);
            if (!localPawn) return;

            uint32_t flags = 0;
            if (!Memory::SafeRead(localPawn + OFF_FLAGS, flags)) return;

            bool onGround = (flags & 1u) != 0;

            if (onGround) {
                if (!s_wasOnGround || s_needsRelease) {
                    // Fresh landing OR we haven't sent the RELEASED transition yet:
                    // Send RELEASED this iteration so next iteration PRESSED is a real transition
                    Memory::SafeWrite<uint32_t>(clientBase + cs2_dumper::buttons::jump, BTN_RELEASED);
                    s_needsRelease = false;
                } else {
                    // Previous iter was RELEASED (from above), now send PRESSED = valid new jump
                    Memory::SafeWrite<uint32_t>(clientBase + cs2_dumper::buttons::jump, BTN_PRESSED);
                    s_needsRelease = true; // must release next iter before pressing again
                }
            } else {
                // In air: always release
                Memory::SafeWrite<uint32_t>(clientBase + cs2_dumper::buttons::jump, BTN_RELEASED);
                s_needsRelease = false;
            }

            s_wasOnGround = onGround;

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            uintptr_t cb = GetClientBase();
            if (cb) Memory::SafeWrite<uint32_t>(cb + cs2_dumper::buttons::jump, BTN_RELEASED);
            s_wasOnGround  = false;
            s_needsRelease = false;
        }
    }

    void RunGameThread() { /* nothing needed */ }

    void Reset() {
        uintptr_t clientBase = GetClientBase();
        if (clientBase)
            Memory::SafeWrite<uint32_t>(clientBase + cs2_dumper::buttons::jump, BTN_RELEASED);
        s_wasOnGround  = false;
        s_needsRelease = false;
    }

} // namespace Bunnyhop
