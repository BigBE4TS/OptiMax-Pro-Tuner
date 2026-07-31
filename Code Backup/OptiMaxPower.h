#pragma once
#include <windows.h>
#include <powrprof.h>
#include <string>
#include <mutex>
#include "OptiMaxStateCache.h"
#pragma comment(lib, "powrprof.lib")

namespace OptiMax {

    // ======================================================================
    // GAME SESSION POWER REQUEST
    //
    // THE FEATURE: while a game is running, tell Windows the machine is in use.
    //
    // Why this exists: a user reported their game freezing 2-3 times over a
    // two-hour session. It was not OptiMax - the process used 1.3% of one core
    // and logged nothing - it was a Microsoft Defender scan, and its own event
    // record is the whole argument:
    //
    //     Scan Trigger: Scheduled maintenance
    //     Scan Only If Idle: Enabled          <-- Windows thought the PC was idle
    //     Low CPU Priority for Scans: Disabled
    //     ScanAvgCPULoadFactor: 50
    //
    // It ran for 33 seconds in the middle of a match. Windows decided an actively
    // played game was an idle machine, and started work it had explicitly been
    // told to only do when idle.
    //
    // A power request is the documented way to say otherwise. It is deliberately
    // the ONLY mechanism used here:
    //   - It opens no handle on the game, so it is safe with a kernel anti-cheat
    //     watching and needs none of the gating the per-game tweaks require.
    //   - The OS drops it automatically if this process dies. Unlike every
    //     registry tweak in this codebase it cannot leak a persistent change,
    //     which matters given how much of the undo surface exists to fix exactly
    //     that.
    //   - It is benign. Reconfiguring Defender, disabling Windows Update or
    //     stopping security services would attack the same symptom and are among
    //     the most recognisable malware behaviours there are. This app is
    //     unsigned, sits at 3/70 on VirusTotal, and its Defender clearance is
    //     bound to one file hash. Shipping AV-suppression code would plausibly
    //     re-trigger the ML detections and cost far more than the feature is
    //     worth. Do not "improve" this by reaching for those.
    //
    // PowerRequestExecutionRequired is deliberately NOT requested: it governs
    // process lifetime management for Store apps and is not meaningful for a
    // Win32 desktop process, so asking for it only invites a failure to handle.
    // ======================================================================
    namespace GameSession {

        inline HANDLE g_powerRequest = NULL;
        inline std::mutex g_powerRequestMutex;

        // Idempotent: a second Begin while one is already held does nothing, so a
        // re-detection cannot stack requests that then need matching releases.
        inline void Begin(const std::wstring& gameName) {
            std::lock_guard<std::mutex> lock(g_powerRequestMutex);
            if (g_powerRequest) return;

            // Shown verbatim by `powercfg /requests`, which is how a user (or the
            // next person debugging this) can see that it is actually held.
            std::wstring reason = L"OptiMax: game session active";
            if (!gameName.empty()) reason += L" (" + gameName + L")";

            REASON_CONTEXT ctx = {};
            ctx.Version = POWER_REQUEST_CONTEXT_VERSION;
            ctx.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
            ctx.Reason.SimpleReasonString = const_cast<LPWSTR>(reason.c_str());

            HANDLE h = PowerCreateRequest(&ctx);
            if (h == NULL || h == INVALID_HANDLE_VALUE) {
                SafeAddLog("POWER: Could not tell Windows the PC is in use - background "
                    "maintenance may still start during a game.");
                return;
            }

            // SystemRequired is the one that matters: it marks the system as in
            // use, which is what idle-triggered work consults. DisplayRequired is
            // included because a game with long low-input stretches should not
            // have the screen blank on it either.
            BOOL sysOk = PowerSetRequest(h, PowerRequestSystemRequired);
            PowerSetRequest(h, PowerRequestDisplayRequired);

            if (!sysOk) {
                CloseHandle(h);
                SafeAddLog("POWER: Windows refused the in-use request - background "
                    "maintenance may still start during a game.");
                return;
            }

            g_powerRequest = h;
            SafeAddLog("POWER: Told Windows the PC is in use for this session - "
                "scheduled scans and maintenance should hold off until the game closes.");
        }

        inline void End() {
            std::lock_guard<std::mutex> lock(g_powerRequestMutex);
            if (!g_powerRequest) return;

            PowerClearRequest(g_powerRequest, PowerRequestDisplayRequired);
            PowerClearRequest(g_powerRequest, PowerRequestSystemRequired);
            CloseHandle(g_powerRequest);
            g_powerRequest = NULL;
            SafeAddLog("POWER: Released the in-use hold - Windows may resume normal "
                "background maintenance.");
        }
    }

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

        // THE FIX: every call below used to be fire-and-forget - three power API
        // results discarded, and no answer for the caller. RestoreProcessorPowerSettings()
        // then logged "returned to their previous values" unconditionally, which it
        // could not know. That is this project's documented worst defect class (a UI
        // claiming success for something that did not happen) sitting on an undo path,
        // which is the worst place for it: a user told their settings were restored
        // has no reason to check, and core parking stays disabled forever.
        //
        // Now every step is checked and the result returned. Callers report what
        // actually happened.
        inline bool ApplySetting(const GUID& setting, DWORD acValue, DWORD dcValue) {
            GUID subGroup = GUID_PROCESSOR_SETTINGS_SUBGROUP;
            GUID* activePlan = nullptr;
            if (PowerGetActiveScheme(NULL, &activePlan) != ERROR_SUCCESS || !activePlan) return false;

            bool ok = (PowerWriteACValueIndex(NULL, activePlan, &subGroup, &setting, acValue) == ERROR_SUCCESS);
            ok = (PowerWriteDCValueIndex(NULL, activePlan, &subGroup, &setting, dcValue) == ERROR_SUCCESS) && ok;

            // Re-activating the scheme is what makes a written value take effect, so a
            // failure here means the values are on disk but not in force - not a success.
            ok = (PowerSetActiveScheme(NULL, activePlan) == ERROR_SUCCESS) && ok;

            LocalFree(activePlan);
            return ok;
        }

        // THE FIX: this used to call PowerWriteACValueIndex only. On a laptop
        // running on battery, that value is never consulted - Windows reads the
        // DC value instead - so this tweak silently did nothing for anyone not
        // plugged in. Now writes both.
        inline bool DisableCoreParking() {
            CaptureOriginal(L"power::cpmincores::ac", L"power::cpmincores::dc", kCpMinCores, 100);
            if (ApplySetting(kCpMinCores, 100, 100)) return true;
            SafeAddLog("POWER: Could not disable core parking - Windows refused the change. "
                "Cores may still be parked.");
            return false;
        }

        inline bool SetEPPToPerformance() {
            CaptureOriginal(L"power::epp::ac", L"power::epp::dc", kEppSetting, 0);
            if (ApplySetting(kEppSetting, 0, 0)) return true;
            SafeAddLog("POWER: Could not set the energy performance preference - Windows "
                "refused the change.");
            return false;
        }

        // Fallbacks are Windows' stock Balanced-plan values, and are only ever reached
        // for a user who applied the tweak with a build that predates the capture above.
        // THE FIX (second one here): the persisted originals used to be cleared
        // unconditionally, even when the write that was supposed to consume them had
        // failed. That destroyed the only record of the user's real prior value, so
        // every later retry fell back to the hardcoded guess below and could never
        // restore what they actually had. The cache is now cleared only once the
        // values are known to be in force.
        inline bool RestoreCoreParking() {
            DWORD ac = 5, dc = 5;
            StateCache::LoadPersistedValue(L"power::cpmincores::ac", ac);
            StateCache::LoadPersistedValue(L"power::cpmincores::dc", dc);
            if (!ApplySetting(kCpMinCores, ac, dc)) return false;
            StateCache::ClearPersistedValue(L"power::cpmincores::ac");
            StateCache::ClearPersistedValue(L"power::cpmincores::dc");
            return true;
        }

        inline bool RestoreEPP() {
            DWORD ac = 50, dc = 50;
            StateCache::LoadPersistedValue(L"power::epp::ac", ac);
            StateCache::LoadPersistedValue(L"power::epp::dc", dc);
            if (!ApplySetting(kEppSetting, ac, dc)) return false;
            StateCache::ClearPersistedValue(L"power::epp::ac");
            StateCache::ClearPersistedValue(L"power::epp::dc");
            return true;
        }

        // Says what happened, including "nothing did". Partial success is reported as
        // partial rather than rounded up to success - restoring one of the two and
        // claiming both is how a user ends up with a permanently pinned EPP and no idea.
        inline bool RestoreProcessorPowerSettings() {
            const bool cores = RestoreCoreParking();
            const bool epp = RestoreEPP();

            if (cores && epp) {
                SafeAddLog("RESTORE: Core parking and the energy performance preference returned to their previous values.");
            }
            else if (cores) {
                SafeAddLog("RESTORE: Core parking was returned to its previous value, but the energy "
                    "performance preference could not be - it is still pinned to maximum performance.");
            }
            else if (epp) {
                SafeAddLog("RESTORE: The energy performance preference was returned to its previous value, "
                    "but core parking could not be - cores are still unparked.");
            }
            else {
                SafeAddLog("RESTORE: Neither core parking nor the energy performance preference could be "
                    "restored - Windows refused the change, so both are unchanged.");
            }
            return cores && epp;
        }
    }
}
