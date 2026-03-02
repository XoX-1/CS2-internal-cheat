#include "nosmoke.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>
#include <string.h>

namespace NoSmoke {

    static bool SafeReadString(uintptr_t addr, char* out, size_t size) {
        __try {
            if (!addr) return false;
            SIZE_T bytesRead = 0;
            return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr), out, size, &bytesRead) && bytesRead > 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void Run() {
        if (!Hooks::g_bNoSmokeEnabled.load()) return;

        __try {
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) return;

            uintptr_t entityList = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || 
                !Memory::IsValidPtr(entityList)) return;

            // Iterate through possible entity indices (grenades can be higher index than players)
            for (int i = Constants::EntityList::MAX_PLAYERS + 1; i < Constants::EntityList::MAX_ENTITIES; i++) {
                uintptr_t listEntry = 0;
                if (!Memory::SafeRead(entityList + Constants::EntityList::OFFSET_BASE + 
                                      sizeof(uintptr_t) * (i >> Constants::EntityList::ENTRY_SHIFT), listEntry) || 
                    !Memory::IsValidPtr(listEntry)) continue;

                uintptr_t entity = 0;
                if (!Memory::SafeRead(listEntry + Constants::EntityList::ENTRY_SIZE * (i & Constants::EntityList::INDEX_MASK), 
                                      entity) || !Memory::IsValidPtr(entity)) continue;

                uintptr_t entityIdentity = 0;
                if (!Memory::SafeRead(entity + Constants::Offsets::ENTITY_IDENTITY, entityIdentity) || 
                    !Memory::IsValidPtr(entityIdentity)) continue;

                uintptr_t pDesignerName = 0;
                if (!Memory::SafeRead(entityIdentity + Constants::Offsets::DESIGNER_NAME_PTR, pDesignerName) || 
                    !Memory::IsValidPtr(pDesignerName)) continue;

                char name[64] = { 0 };
                if (SafeReadString(pDesignerName, name, sizeof(name))) {
                    // Check if this is a smoke grenade projectile
                    if (strstr(name, "smokegrenade") || strstr(name, "Smoke")) {
                        // Remove smoke by setting its lifetime to 0
                        Memory::SafeWrite<float>(entity + cs2_dumper::schemas::client_dll::C_SmokeGrenadeProjectile::m_nSmokeEffectTickBegin, 0.0f);
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silently recover
        }
    }
}
