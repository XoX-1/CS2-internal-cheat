#pragma once

#include <cstddef>
#include <cstdint>

namespace Constants {

    // Entity list traversal constants
    namespace EntityList {
        constexpr size_t OFFSET_BASE = 0x10;
        constexpr size_t ENTRY_SHIFT = 9;
        constexpr size_t ENTRY_SIZE = 0x70;
        constexpr size_t INDEX_MASK = 0x1FF;
        constexpr size_t HANDLE_MASK = 0x7FFF;
        constexpr size_t MAX_PLAYERS = 64;
        constexpr size_t MAX_ENTITIES = 1024;
    }

    // Bone structure constants
    namespace Bones {
        constexpr int ROOT = 0;
        constexpr int PELVIS = 2;
        constexpr int STOMACH = 3;
        constexpr int SPINE = 4;
        constexpr int NECK = 5;
        constexpr int HEAD = 6;
        constexpr int LEFT_SHOULDER = 8;
        constexpr int LEFT_ELBOW = 9;
        constexpr int LEFT_HAND = 10;
        constexpr int RIGHT_SHOULDER = 13;
        constexpr int RIGHT_ELBOW = 14;
        constexpr int RIGHT_HAND = 15;
        constexpr int LEFT_HIP = 22;
        constexpr int LEFT_KNEE = 23;
        constexpr int LEFT_FOOT = 24;
        constexpr int RIGHT_HIP = 25;
        constexpr int RIGHT_KNEE = 26;
        constexpr int RIGHT_FOOT = 27;
        
        constexpr size_t BONE_SIZE = 32; // Size of each bone entry in bytes
        constexpr float HEAD_HEIGHT = 72.0f; // Approximate height offset for head
    }

    // Game constants
    namespace Game {
        constexpr uint32_t FL_ONGROUND = (1 << 0);
        constexpr int LIFE_ALIVE = 0;
        constexpr int MAX_HEALTH = 100;
        constexpr int DEFAULT_FOV = 90;
        constexpr float MAX_AIM_PITCH = 89.0f;
        constexpr float MIN_AIM_PITCH = -89.0f;
        constexpr float MAX_AIM_YAW = 180.0f;
        constexpr float MIN_AIM_YAW = -180.0f;
    }

    // Memory offsets within structures (when not using dumper)
    namespace Offsets {
        // Entity identity / designer name
        constexpr size_t ENTITY_IDENTITY = 0x10;
        constexpr size_t DESIGNER_NAME_PTR = 0x20;
        
        // Bone array offset from model state
        constexpr size_t BONE_ARRAY_OFFSET = 0x80;
    }

    // Input keys
    namespace Keys {
        constexpr int MENU = VK_INSERT;     // INSERT key - hardcoded for menu toggle
        constexpr int UNLOAD = VK_END;      // END key - hardcoded for unload
        constexpr int BHOP = VK_SPACE;      // SPACE key - hardcoded for bhop
        // Note: AIMBOT and TRIGGERBOT keys are now configurable via KeybindManager
    }

    // Timing constants (milliseconds)
    namespace Timing {
        constexpr int THREAD_SLEEP_MS = 1;
        constexpr int MODULE_WAIT_MS = 100;
        constexpr int INIT_DELAY_MS = 1000;
        constexpr int TRIGGER_DELAY_MS = 5;
        constexpr int BURST_DELAY_MS = 10;
        constexpr int STATE_CHECK_INTERVAL = 100;
        constexpr int RESTORE_ATTEMPTS_MAX = 100;
    }

} // namespace Constants