#pragma once
#include <windows.h>
#include <powrprof.h>
#include "OptiMaxStateCache.h"
#pragma comment(lib, "powrprof.lib")

namespace OptiMax {
    namespace AdvancedPower {

        // Documented setting GUIDs. CPMINCORES is "Processor performance core parking
        // min cores" - forcing it to 100 means "never park any core", which is what
        // the Unpark Cores tweak is.
        inline const GUID kCpMinCores = { 0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83} };
        inline const GUID kEppSetting = { 0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63} };

        // THE FIX: nothing in the program could undo either of the two tweaks below.
        // There was no restore function here, the "Unpark CPU Cores" toggle's off
        // branch had nothing to call, and "Restore Windows Defaults" skipped them
        // entirely - so once applied, core parking stayed disabled and the energy
        // performance preference stayed pinned at maximum performance forever, on
        // both AC and battery. On a laptop that is a permanent, invisible hit to
        // battery life that survives uninstalling the app.
        //
        // Rather than assuming what the user's values were, the originals are read
        // and persisted (to the same HKCU store the service cache uses) before the
        // first overwrite, so a restore returns their real prior settings. Values are
        // captured once and only once - re-applying the tweak must never "capture"
        // the value OptiMax itself has already written.
        // `tweakValue` is what the caller is about to write. A current value that already
        // equals it is NOT recorded as the original.
        //
        // This matters on any machine where an earlier build of OptiMax already applied
        // the tweak - which is every existing install. Capturing there would record 100
        // (or EPP 0) as "the user's original", and every future restore would put the
        // tweak straight back and report success. Since the value we write is its own
        // sentinel, "already exactly our value" is indistinguishable from "the user
        // chose our value", and the safe reading of an ambiguous case is to keep no
        // original at all and fall back to the documented Windows default.
        inline void CaptureOriginal(const wchar_t* keyAc, const wchar_t* keyDc, const GUID& setting, DWORD tweakValue) {
            DWORD existing = 0;
            if (StateCache::LoadPersistedValue(keyAc, existing)) return;

            GUID subGroup = GUID_PROCESSOR_SETTINGS_SUBGROUP;
            GUID* activePlan = nullptr;
            if (PowerGetActiveScheme(NULL, &activePlan) == ERROR_SUCCESS && activePlan) {
                DWORD ac = 0, dc = 0;
                if (PowerReadACValueIndex(NULL, activePlan, &subGroup, &setting, &ac) == ERROR_SUCCESS && ac != tweakValue)
                    StateCache::PersistValue(keyAc, ac);
                if (PowerReadDCValueIndex(NULL, activePlan, &subGroup, &setting, &dc) == ERROR_SUCCESS && dc != tweakValue)
                    StateCache::PersistValue(keyDc, dc);
                LocalFree(activePlan);
            }
        }

        inline void ApplySetting(const GUID& setting, DWORD acValue, DWORD dcValue) {
            GUID subGroup = GUID_PROCESSOR_SETTINGS_SUBGROUP;
            GUID* activePlan = nullptr;
            if (PowerGetActiveScheme(NULL, &activePlan) == ERROR_SUCCESS && activePlan) {
                PowerWriteACValueIndex(NULL, activePlan, &subGroup, &setting, acValue);
                PowerWriteDCValueIndex(NULL, activePlan, &subGroup, &setting, dcValue);
                // Re-activating the scheme is what makes a written value take effect.
                PowerSetActiveScheme(NULL, activePlan);
                LocalFree(activePlan);
            }
        }

        // THE FIX: this used to call PowerWriteACValueIndex only. On a laptop
        // running on battery, that value is never consulted - Windows reads the
        // DC value instead - so this tweak silently did nothing for anyone not
        // plugged in. Now writes both.
        inline void DisableCoreParking() {
            CaptureOriginal(L"power::cpmincores::ac", L"power::cpmincores::dc", kCpMinCores, 100);
            ApplySetting(kCpMinCores, 100, 100);
        }

        inline void SetEPPToPerformance() {
            CaptureOriginal(L"power::epp::ac", L"power::epp::dc", kEppSetting, 0);
            ApplySetting(kEppSetting, 0, 0);
        }

        // Fallbacks are Windows' stock Balanced-plan values, and are only ever reached
        // for a user who applied the tweak with a build that predates the capture above.
        inline void RestoreCoreParking() {
            DWORD ac = 5, dc = 5;
            StateCache::LoadPersistedValue(L"power::cpmincores::ac", ac);
            StateCache::LoadPersistedValue(L"power::cpmincores::dc", dc);
            ApplySetting(kCpMinCores, ac, dc);
            StateCache::ClearPersistedValue(L"power::cpmincores::ac");
            StateCache::ClearPersistedValue(L"power::cpmincores::dc");
        }

        inline void RestoreEPP() {
            DWORD ac = 50, dc = 50;
            StateCache::LoadPersistedValue(L"power::epp::ac", ac);
            StateCache::LoadPersistedValue(L"power::epp::dc", dc);
            ApplySetting(kEppSetting, ac, dc);
            StateCache::ClearPersistedValue(L"power::epp::ac");
            StateCache::ClearPersistedValue(L"power::epp::dc");
        }

        inline void RestoreProcessorPowerSettings() {
            RestoreCoreParking();
            RestoreEPP();
            SafeAddLog("RESTORE: Core parking and the energy performance preference returned to their previous values.");
        }
    }
}
