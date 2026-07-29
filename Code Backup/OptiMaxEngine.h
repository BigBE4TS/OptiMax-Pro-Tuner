#pragma once
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <iostream>
#include <winternl.h>
#include <powrprof.h>
#include <vector>
#include <setupapi.h>
#include <devguid.h>
#include <string>
#include <cfgmgr32.h>
#include <tlhelp32.h>
#include <fstream>
#include <ctime>
#include <srrestoreptapi.h> 
#include <psapi.h>
#include <mmsystem.h>
#include <algorithm>
#include <processthreadsapi.h> // PROCESS_POWER_THROTTLING_STATE / SetProcessInformation
// ToggleExplorer() uses SHELLEXECUTEINFOW / ShellExecuteExW. WIN32_LEAN_AND_MEAN
// (defined by several headers in this project) excludes shellapi.h from windows.h,
// so this compiled only because some other header happened to pull it in first.
#include <shellapi.h>

// --- LINK NEW GLOBAL STATE ---
#include "OptiMaxGlobals.h"
// -----------------------------

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "srclient.lib") 
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winmm.lib") 

#ifndef MAX_DEVICE_ID_LEN
#define MAX_DEVICE_ID_LEN 200
#endif

extern "C" {
    __kernel_entry NTSTATUS NTAPI NtQueryTimerResolution(_Out_ PULONG Min, _Out_ PULONG Max, _Out_ PULONG Current);
    __kernel_entry NTSTATUS NTAPI NtSetTimerResolution(_In_ ULONG Desired, _In_ BOOLEAN Set, _Out_ PULONG Current);
    __kernel_entry NTSTATUS NTAPI NtSetInformationProcess(_In_ HANDLE ProcessHandle, _In_ ULONG ProcessInformationClass, _In_ PVOID ProcessInformation, _In_ ULONG ProcessInformationLength);
    typedef NTSTATUS(WINAPI* NtSetSystemInformation_t)(INT, PVOID, ULONG);
}

extern void SafeAddLog(const std::string& msg);

namespace OptiMax {

    struct ScopedHandle {
        HANDLE h;
        ScopedHandle(HANDLE handle) : h(handle) {}
        ~ScopedHandle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
        operator HANDLE() const { return h; }
    };

    inline bool IsElevated() {
        HANDLE hRawToken = NULL;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hRawToken)) return false;
        ScopedHandle hToken(hRawToken);

        TOKEN_ELEVATION elevation; DWORD size = sizeof(TOKEN_ELEVATION);
        if (!GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &size)) {
            return false;
        }
        return elevation.TokenIsElevated != 0;
    }

    inline bool IsAntiCheatRunning() {
        // THE FIX: a fixed DWORD[1024] buffer means EnumProcesses silently truncates
        // on any system with more than 1024 running processes - a real anti-cheat
        // could go undetected on a busy machine. Grow the buffer until it's big
        // enough to hold everything EnumProcesses reports.
        std::vector<DWORD> aProcesses(1024);
        DWORD cbNeeded = 0;
        while (true) {
            if (!EnumProcesses(aProcesses.data(), (DWORD)(aProcesses.size() * sizeof(DWORD)), &cbNeeded)) return false;
            if (cbNeeded < aProcesses.size() * sizeof(DWORD)) break; // buffer was big enough
            if (aProcesses.size() >= 65536) break; // sane upper bound, shouldn't realistically be hit
            aProcesses.resize(aProcesses.size() * 2);
        }

        DWORD cProcesses = cbNeeded / sizeof(DWORD);
        std::vector<std::wstring> ac_processes = { L"vgc.exe", L"EasyAntiCheat.exe", L"EasyAntiCheat_EOS.exe", L"BEService.exe", L"FaceItClient.exe", L"gamersclub.exe" };

        for (unsigned int i = 0; i < cProcesses; i++) {
            if (aProcesses[i] != 0) {
                ScopedHandle hProcess(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, aProcesses[i]));
                if (hProcess) {
                    wchar_t szProcessName[MAX_PATH];
                    DWORD size = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProcess, 0, szProcessName, &size)) {
                        for (const auto& ac : ac_processes) {
                            if (wcsstr(szProcessName, ac.c_str()) != nullptr) return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    // THE FIX: SaveSettings()/LoadSettings() used to live here. They were dead code -
    // nothing in the program ever called either one - and they were also broken:
    // "config.opti" is a relative path, so they resolved against the current working
    // directory. This app self-elevates and is launched from a logon task, both of
    // which leave the CWD as C:\Windows\system32, so any settings they had written
    // would have gone somewhere the app would never look again. Real persistence is
    // SavePersistentState()/LoadPersistentState() in OptiMax_Profiler.cpp, which uses
    // an absolute path. Removed rather than fixed so nobody wires them up by mistake.

    inline void CreateRestorePoint() {
        RESTOREPOINTINFOW rpi = { BEGIN_SYSTEM_CHANGE, APPLICATION_INSTALL, 0, L"Before OptiMax tweaks" };
        STATEMGRSTATUS status = {};
        BOOL ok = SRSetRestorePointW(&rpi, &status);
        // THE FIX: this return value (and status.nStatus) used to be discarded entirely.
        // Restore point creation is commonly throttled by Windows or blocked if System
        // Restore is disabled - given how many registry/service/driver changes this app
        // makes, silently failing here means the user has no rollback safety net and no
        // idea. Now it's at least logged either way.
        if (ok && status.nStatus == ERROR_SUCCESS) {
            // THE FIX: BEGIN_SYSTEM_CHANGE was never matched with END_SYSTEM_CHANGE, so
            // every restore point this app created was left open. Windows keeps an
            // unended restore point in an in-progress state, and System Restore will
            // not offer an in-progress point to the user - which means the safety net
            // the log claimed to have created was not actually selectable when needed.
            // The shadow copy is taken during the BEGIN call, so ending it immediately
            // is correct: the snapshot already reflects the pre-tweak system.
            RESTOREPOINTINFOW endRpi = { END_SYSTEM_CHANGE, APPLICATION_INSTALL, status.llSequenceNumber, L"Before OptiMax tweaks" };
            STATEMGRSTATUS endStatus = {};
            if (SRSetRestorePointW(&endRpi, &endStatus) && endStatus.nStatus == ERROR_SUCCESS) {
                SafeAddLog("SYSTEM: Restore point created successfully.");
            }
            else {
                SafeAddLog("WARNING: Restore point was started but could not be finalised (status " + std::to_string(endStatus.nStatus) +
                    "). It may not appear in System Restore. Proceeding without a reliable rollback safety net.");
            }
        }
        else {
            SafeAddLog("WARNING: Restore point creation failed (status " + std::to_string(status.nStatus) +
                "). System Restore may be disabled, or Windows is throttling restore point frequency. Proceeding without a rollback safety net.");
        }
    }

    class HighResTimer {
        bool active = false;
    public:
        ~HighResTimer() { disable(); }
        void enable() { if (!active) { timeBeginPeriod(1); active = true; } }
        void disable() { if (active) { timeEndPeriod(1); active = false; } }
    };
    inline HighResTimer g_SysTimer;

    // Tracks whether the undocumented NtSetTimerResolution path is currently holding
    // a resolution request, so the release can be issued through the same path that
    // acquired it and never through the other one.
    inline bool g_ntTimerEngaged = false;

    // THE FIX: this used to branch on safeMode for BOTH directions. Callers pass the
    // live anti-cheat state when enabling (SetTimer(true, currentAntiCheat)), but
    // RevertOptimizations() always passed safeMode=false when disabling. So on a
    // machine with an anti-cheat running, the timer was acquired via timeBeginPeriod(1)
    // and the disable went down the Nt branch instead - timeEndPeriod(1) was never
    // called and the 1 ms system-wide timer period stayed raised for the life of the
    // process (measurably worse idle power and DPC behaviour), while the Nt release ran
    // with no matching acquire. Disabling now tears down whichever path is actually
    // holding a request, and safe mode still never touches the Nt API.
    inline void SetTimer(bool enable, bool safeMode) {
        if (enable) {
            if (safeMode) {
                g_SysTimer.enable();
            }
            else {
                ULONG res;
                if (NtSetTimerResolution(5000, TRUE, &res) >= 0) g_ntTimerEngaged = true;
            }
        }
        else {
            g_SysTimer.disable();
            if (g_ntTimerEngaged) {
                ULONG res;
                NtSetTimerResolution(5000, FALSE, &res);
                g_ntTimerEngaged = false;
            }
        }
    }

    inline bool IsExplorerRunning() {
        bool running = false;
        ScopedHandle hSnap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = { sizeof(pe) };
            if (Process32FirstW(hSnap, &pe)) {
                do { if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) { running = true; break; } } while (Process32NextW(hSnap, &pe));
            }
        }
        return running;
    }

    // THE FIX: Explorer coming back on its own isn't your code failing - it's
    // Windows itself. HKLM\...\Winlogon\AutoRestartShell is a built-in watchdog
    // that relaunches the shell whenever it terminates, completely independent
    // of anything OptiMax does. Requires admin rights (it's under HKLM), and
    // won't take effect on an already-running session in every case, but this
    // is the documented, correct switch to actually keep Explorer down.
    inline void SetAutoRestartShell(bool enabled) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            DWORD val = enabled ? 1 : 0;
            LONG res = RegSetValueExW(hKey, L"AutoRestartShell", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
            RegCloseKey(hKey);
            if (res != ERROR_SUCCESS) {
                SafeAddLog("SHELL WARNING: Could not update AutoRestartShell - run as admin for Explorer to reliably stay off.");
            }
        }
        else {
            SafeAddLog("SHELL WARNING: Could not access Winlogon key - run as admin for Explorer to reliably stay off.");
        }
    }

    // THE FIX: this used to shell out via system("taskkill /F /IM explorer.exe"),
    // which spawns a full cmd.exe just to kill one process, is slower, and can
    // briefly flash a console window even in a /SUBSYSTEM:WINDOWS build. Finding
    // Explorer's PID directly and calling TerminateProcess is more direct and
    // reliable. It now also toggles AutoRestartShell alongside the kill/restore,
    // since that's the actual thing bringing Explorer back on its own.
    inline void ToggleExplorer() {
        if (IsExplorerRunning()) {
            SetAutoRestartShell(false);

            ScopedHandle hSnap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe = { sizeof(pe) };
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                            ScopedHandle hProc(OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID));
                            if (hProc) TerminateProcess(hProc, 0);
                        }
                    } while (Process32NextW(hSnap, &pe));
                }
            }
        }
        else {
            SetAutoRestartShell(true);

            // THE FIX: ShellExecuteW is fire-and-forget - it returns the instant
            // explorer.exe is launched, not once it's actually finished initializing
            // its taskbar/notification-area services. Anything that immediately
            // depends on Explorer being fully up (like registering a tray icon right
            // after, which MinimizeToTray does) could silently fail if it runs before
            // Explorer's shell services are ready - leaving a hidden window with no
            // tray icon to bring it back, which looks exactly like the app closed.
            // ShellExecuteExW + WaitForInputIdle is the correct, standard API for
            // "wait until this GUI process can actually accept input," rather than
            // guessing with a fixed sleep.
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = L"open";
            sei.lpFile = L"explorer.exe";
            sei.nShow = SW_SHOW;

            if (ShellExecuteExW(&sei) && sei.hProcess) {
                WaitForInputIdle(sei.hProcess, 5000);
                CloseHandle(sei.hProcess);
            }
        }
    }

    inline void SetGPUPriorityHigh() {
        HDEVINFO hDevInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, NULL, NULL, DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) return;
        SP_DEVINFO_DATA devInfoData = { sizeof(SP_DEVINFO_DATA) };
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
            wchar_t id[MAX_DEVICE_ID_LEN];
            if (SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, id, MAX_DEVICE_ID_LEN, NULL)) {
                std::wstring path = L"SYSTEM\\CurrentControlSet\\Enum\\" + std::wstring(id) + L"\\Device Parameters\\Interrupt Management\\Affinity Policy";
                HKEY hKey;
                if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                    DWORD p = 2;
                    if (RegSetValueExW(hKey, L"DevicePriority", 0, REG_DWORD, (const BYTE*)&p, sizeof(p)) != ERROR_SUCCESS) {
                        SafeAddLog("SYSTEM WARNING: Failed to force GPU IRQ Priority.");
                    }
                    RegCloseKey(hKey);
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    // (namespace Precision / ExactSleep() lived here and was removed - a
    // hybrid waitable-timer-then-spin sleep with zero call sites. Worth knowing
    // it existed if a future frame limiter needs one: it burned a full core in
    // the spin phase, so it is not a free thing to reintroduce.)

    namespace Power {
        inline GUID originalPowerPlan = {};
        inline const GUID GUID_ULTIMATE_PERFORMANCE = { 0xe9a42b02, 0xd5df, 0x448d, { 0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61 } };
        // Windows' built-in High Performance scheme. Declared locally rather than using
        // winnt.h's GUID_MIN_POWER_SAVINGS, which is a DEFINE_GUID that only produces a
        // definition in a translation unit that defines INITGUID.
        inline const GUID GUID_HIGH_PERFORMANCE = { 0x8c5e7fda, 0xe8bf, 0x4a96, { 0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c } };
        inline bool isPowerOverridden = false;
        inline bool hasOriginalPlan = false;

        inline void EngageUltimatePower() {
            // THE FIX (1/3): this captured the active scheme on EVERY call. The second
            // call - and this is reached from three separate places, including the
            // per-game auto-boost path that can fire repeatedly in one session - saved
            // the plan OptiMax had *already* switched to as if it were the user's
            // original. After that, restoring could never get back to their real plan.
            if (!isPowerOverridden) {
                GUID* activePlan = nullptr;
                if (PowerGetActiveScheme(NULL, &activePlan) == ERROR_SUCCESS && activePlan) {
                    originalPowerPlan = *activePlan;
                    hasOriginalPlan = true;
                    LocalFree(activePlan);
                }
            }

            // THE FIX (2/3): the Ultimate Performance scheme does not exist on a stock
            // Windows install - it has to be duplicated in first, which is exactly what
            // `powercfg -duplicatescheme e9a42b02-...` does. On the majority of machines
            // (and on every laptop, where Windows hides it) PowerSetActiveScheme simply
            // returned ERROR_FILE_NOT_FOUND, the power plan never changed at all, and
            // the caller logged that the system was now on Ultimate Performance. Try the
            // scheme, duplicate it in if it's missing, and fall back to High Performance
            // (always present) rather than silently leaving the user on Balanced.
            DWORD res = PowerSetActiveScheme(NULL, &GUID_ULTIMATE_PERFORMANCE);
            if (res != ERROR_SUCCESS) {
                GUID* duplicated = nullptr;
                if (PowerDuplicateScheme(NULL, &GUID_ULTIMATE_PERFORMANCE, &duplicated) == ERROR_SUCCESS && duplicated) {
                    res = PowerSetActiveScheme(NULL, duplicated);
                    LocalFree(duplicated);
                }
            }
            if (res != ERROR_SUCCESS) {
                res = PowerSetActiveScheme(NULL, &GUID_HIGH_PERFORMANCE);
                if (res == ERROR_SUCCESS) {
                    SafeAddLog("POWER: Ultimate Performance is not available on this system; switched to High Performance instead.");
                }
            }

            // THE FIX (3/3): isPowerOverridden was set unconditionally, so a failed
            // switch still armed the restore path to write a plan we never left.
            if (res == ERROR_SUCCESS) {
                isPowerOverridden = true;
            }
            else {
                SafeAddLog("POWER WARNING: Could not change the active power plan - administrator rights are required, or power policy is managed by group policy.");
            }
        }

        inline void RestorePowerPlan() {
            // hasOriginalPlan guards the case where PowerGetActiveScheme failed: without
            // it, originalPowerPlan is all zeroes and this would ask Windows to activate
            // a scheme GUID that does not exist.
            if (isPowerOverridden && hasOriginalPlan) {
                PowerSetActiveScheme(NULL, &originalPowerPlan);
            }
            isPowerOverridden = false;
        }
    }

    // Forward declaration: real definition lives further down this file.
    // TrueStandbyPurge needs it to avoid trimming critical/whitelisted processes.
    inline bool IsProtectedProcess(const std::wstring& exeName);

    // enable defaults to true so existing call sites are unchanged. Passing false
    // clears the privilege from the token again - used to scope SeDebugPrivilege to
    // just the moment a purge needs it instead of leaving it enabled process-wide,
    // which both softens an AV heuristic and shrinks the window an anti-cheat can
    // observe an elevated-reach process.
    inline void EnableTrimPrivilege(LPCWSTR privilegeName, bool enable = true) {
        HANDLE hRawToken = NULL;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hRawToken)) {
            ScopedHandle hToken(hRawToken);
            TOKEN_PRIVILEGES tp;
            if (LookupPrivilegeValueW(NULL, privilegeName, &tp.Privileges[0].Luid)) {
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;
                AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, 0);
            }
        }
    }

    // Game-aware purge: trims background/idle processes to free RAM, but explicitly
    // leaves the active game process (and anything on the protected whitelist) alone,
    // so its resident textures/assets never get evicted mid-session. Use this instead
    // of TrueStandbyPurge() whenever a game is currently running.
    inline void SmartBackgroundPurge(const std::wstring& activeGameExe) {
        EnableTrimPrivilege(L"SeProfileSingleProcessPrivilege");
        EnableTrimPrivilege(L"SeDebugPrivilege");

        DWORD selfPid = GetCurrentProcessId();

        std::wstring lowerGame = activeGameExe;
        std::transform(lowerGame.begin(), lowerGame.end(), lowerGame.begin(), ::towlower);

        ScopedHandle hSnap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (pe.th32ProcessID == selfPid) continue;

                    std::wstring exeName = pe.szExeFile;
                    if (IsProtectedProcess(exeName)) continue;

                    std::wstring lowerExe = exeName;
                    std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(), ::towlower);

                    // The key difference from TrueStandbyPurge: never touch the game itself.
                    if (!lowerGame.empty() && lowerExe == lowerGame) continue;

                    ScopedHandle hProc(OpenProcess(PROCESS_SET_QUOTA, FALSE, pe.th32ProcessID));
                    if (hProc) {
                        SetProcessWorkingSetSize(hProc, (SIZE_T)-1, (SIZE_T)-1);
                    }
                } while (Process32NextW(hSnap, &pe));
            }
        }

        // Drop SeDebugPrivilege the instant the purge is done - it was only needed to
        // OpenProcess() other users' processes above. No reason to keep it on the token.
        EnableTrimPrivilege(L"SeDebugPrivilege", false);

        // Deliberately skip the global standby-list purge (NtSetSystemInformation /
        // MemoryPurgeStandbyList) that TrueStandbyPurge does. That call drops the
        // OS-wide file cache, which is also what the game's own asset/texture
        // streaming leans on - purging it mid-session tends to show up as texture
        // pop-in or a hitch as data gets re-read from disk. Trimming background
        // processes' private working sets is enough of a win on its own here.
    }

    // Detects a likely game with zero configuration: the foreground window is
    // treated as "a game" if it covers its entire monitor (fullscreen or
    // borderless-fullscreen) and isn't on the protected/system whitelist.
    // Returns the exe name, or an empty string if nothing qualifies.
    // THE FIX: fullscreen alone isn't a strong enough signal for "this is a game" -
    // Discord (theater mode/calls), browsers (fullscreen video), Spotify, OBS,
    // Teams, Zoom, and media players can all go fullscreen without being games.
    // Misclassifying one of these as "the game" would apply game-specific boosts
    // (P-core affinity lock, EcoQoS override) to the wrong process, and would
    // auto-add it to the library. This is an explicit exclusion list, same pattern
    // as the protected-process whitelist elsewhere in this file.
    inline bool IsKnownNonGameApp(const std::wstring& exeName) {
        std::vector<std::wstring> nonGameList = {
            // Communication / calls
            L"discord.exe", L"discordptb.exe", L"discordcanary.exe", L"teams.exe",
            L"ms-teams.exe", L"slack.exe", L"zoom.exe", L"skype.exe", L"webex.exe",
            L"telegram.exe", L"whatsapp.exe",

            // Browsers (fullscreen video is extremely common)
            L"chrome.exe", L"msedge.exe", L"firefox.exe", L"opera.exe",
            L"brave.exe", L"vivaldi.exe",

            // Media / streaming
            L"vlc.exe", L"wmplayer.exe", L"spotify.exe", L"potplayer.exe",
            L"mpc-hc.exe", L"mpc-hc64.exe", L"plex.exe", L"plexmediaplayer.exe",
            L"netflix.exe",

            // Streaming / capture / productivity
            L"obs64.exe", L"obs32.exe", L"powerpnt.exe"
        };

        std::wstring lowerExe = exeName;
        std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(), ::towlower);

        for (const auto& app : nonGameList) {
            if (lowerExe == app) return true;
        }
        return false;
    }

    inline std::wstring GetFullscreenForegroundGame() {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return L"";

        RECT wndRect;
        if (!GetWindowRect(hwnd, &wndRect)) return L"";

        HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfoW(hMon, &mi)) return L"";

        bool isFullscreen = (wndRect.left <= mi.rcMonitor.left &&
            wndRect.top <= mi.rcMonitor.top &&
            wndRect.right >= mi.rcMonitor.right &&
            wndRect.bottom >= mi.rcMonitor.bottom);
        if (!isFullscreen) return L"";

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0 || pid == GetCurrentProcessId()) return L"";

        ScopedHandle hProc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (!hProc) return L"";

        wchar_t exePath[MAX_PATH];
        DWORD size = MAX_PATH;
        if (!QueryFullProcessImageNameW(hProc, 0, exePath, &size)) return L"";

        std::wstring full(exePath);
        size_t pos = full.find_last_of(L"\\/");
        std::wstring exeName = (pos != std::wstring::npos) ? full.substr(pos + 1) : full;

        // Don't treat File Explorer / a maximized browser / etc as a game.
        if (IsProtectedProcess(exeName)) return L"";
        if (IsKnownNonGameApp(exeName)) return L"";

        return exeName;
    }

    // Disables Windows' EcoQoS / Efficiency Mode throttling for every process
    // matching exeName. "Ultimate Performance" power plan alone doesn't stop
    // the scheduler from quietly throttling execution speed on a per-process
    // basis - this is the per-process override that actually guarantees it.
    inline void DisableEfficiencyMode(const std::wstring& exeName) {
        ScopedHandle hSnap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (hSnap == INVALID_HANDLE_VALUE) return;

        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                    ScopedHandle hProc(OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID));
                    if (hProc) {
                        PROCESS_POWER_THROTTLING_STATE state = {};
                        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                        state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                        state.StateMask = 0; // 0 = do not throttle this process
                        if (SetProcessInformation(hProc, ProcessPowerThrottling, &state, sizeof(state))) {
                            SafeAddLog("POWER: EcoQoS/Efficiency Mode disabled for active game process.");
                        }
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
    }

    inline void TrueStandbyPurge() {
        // SeProfileSingleProcessPrivilege only covers trimming your OWN process.
        // SeDebugPrivilege is what actually grants access to trim OTHER processes'
        // working sets. Without it, OpenProcess(PROCESS_SET_QUOTA, ...) silently
        // fails for most processes you don't own, so keep both enabled.
        EnableTrimPrivilege(L"SeProfileSingleProcessPrivilege");
        EnableTrimPrivilege(L"SeDebugPrivilege");

        DWORD selfPid = GetCurrentProcessId();

        ScopedHandle hSnap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    // THE FIX: never blanket-trim every process on the system.
                    // Skip our own process (nothing to gain mid-purge), and skip
                    // anything on the protected whitelist (explorer.exe, dwm.exe,
                    // csrss.exe, lsass.exe, svchost.exe, anti-cheats, etc). Forcing
                    // a full working-set eviction on session-critical processes
                    // simultaneously is what was presenting as "RAM getting wiped" /
                    // the system hitching or hanging right after a purge - it's not
                    // memory corruption, it's every process having to page itself
                    // back in from disk at the same instant.
                    if (pe.th32ProcessID == selfPid) continue;
                    std::wstring exeName = pe.szExeFile;
                    if (IsProtectedProcess(exeName)) continue;

                    ScopedHandle hProc(OpenProcess(PROCESS_SET_QUOTA, FALSE, pe.th32ProcessID));
                    if (hProc) {
                        SetProcessWorkingSetSize(hProc, (SIZE_T)-1, (SIZE_T)-1);
                    }
                } while (Process32NextW(hSnap, &pe));
            }
        }

        // Drop SeDebugPrivilege now that the working-set trim loop is done; the
        // standby-list purge below is a system call that doesn't need it.
        EnableTrimPrivilege(L"SeDebugPrivilege", false);

        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            NtSetSystemInformation_t pNtSetSystemInformation = (NtSetSystemInformation_t)GetProcAddress(ntdll, "NtSetSystemInformation");
            if (pNtSetSystemInformation) {
                int command = 4;
                NTSTATUS st = pNtSetSystemInformation(80, &command, sizeof(command));
                // THE FIX: this undocumented call's NTSTATUS used to be discarded. It's
                // version-sensitive (SystemMemoryListInformation / MemoryPurgeStandbyList),
                // so logging failures here turns "why didn't the purge do anything on this
                // user's Windows build" into an actual diagnosable log line.
                if (st != STATUS_SUCCESS) {
                    SafeAddLog("WARNING: Could not clear the standby memory list (NTSTATUS " + std::to_string((long)st) + "). This Windows build may not support it.");
                }
            }
        }
    }

    // THE FIX: the disable path used to hardcode the Windows defaults (10 / 20). Those
    // same two values are also owned by the Experimental page's "TCP Latency Tweaks"
    // toggle, which sets them to 0xFFFFFFFF / 0 and is a persistent, user-facing
    // setting. RevertOptimizations() runs every time a game exits, on app shutdown and
    // on "Turn Off All Tweaks" - so a user with the TCP tweak switched on silently lost
    // half of it the first time they closed a game, with the toggle still showing as on
    // and nothing in the log. Capture whatever was there before we overwrite it and put
    // that back instead of assuming the stock values.
    inline DWORD g_savedThrottlingIndex = 10;
    inline DWORD g_savedResponsiveness = 20;
    inline bool  g_networkLatencySaved = false;

    inline void OptimizeNetworkLatency(bool enable) {
        const wchar_t* profilePath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";

        // THE FIX (A4-17): the disable path wrote g_saved* unconditionally, but those
        // globals keep whatever was captured last while g_networkLatencySaved is cleared
        // at the end of every disable - so the SECOND disable in a row wrote the tweaked
        // values straight back. RevertOptimizations() runs on every game exit, on app
        // shutdown AND on "Turn Off All Tweaks", so pressing that button and then closing
        // the app silently re-applied the throttling tweak the user had just switched off.
        // RestoreNagleAlgorithm() had already set Windows' 10/20 and logged it truthfully,
        // which made the log look like the liar. With nothing captured there is nothing of
        // ours to undo - leave the values alone rather than asserting a remembered one.
        if (!enable && !g_networkLatencySaved) return;

        if (enable && !g_networkLatencySaved) {
            HKEY hRead;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, profilePath, 0, KEY_QUERY_VALUE, &hRead) == ERROR_SUCCESS) {
                DWORD val = 0, size = sizeof(val), type = 0;
                if (RegQueryValueExW(hRead, L"NetworkThrottlingIndex", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
                    g_savedThrottlingIndex = val;
                size = sizeof(val);
                if (RegQueryValueExW(hRead, L"SystemResponsiveness", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
                    g_savedResponsiveness = val;
                RegCloseKey(hRead);
            }
            g_networkLatencySaved = true;
        }

        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, profilePath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            DWORD throttle = enable ? 0xFFFFFFFF : g_savedThrottlingIndex;
            DWORD response = enable ? 0 : g_savedResponsiveness;
            RegSetValueExW(hKey, L"NetworkThrottlingIndex", 0, REG_DWORD, (const BYTE*)&throttle, sizeof(throttle));
            RegSetValueExW(hKey, L"SystemResponsiveness", 0, REG_DWORD, (const BYTE*)&response, sizeof(response));
            RegCloseKey(hKey);
        }

        if (!enable) {
            // Drop the captured values along with the flag. Leaving the tweaked pair
            // sitting in these globals is what let a later disable re-assert them.
            g_savedThrottlingIndex = 10;
            g_savedResponsiveness = 20;
            g_networkLatencySaved = false;
        }
    }

    // --- PROTECTED PROCESS WHITELIST ---
    inline bool IsProtectedProcess(const std::wstring& exeName) {
        std::vector<std::wstring> protectedList = {
            // Anti-Cheats (Never throttle or they disconnect/ban)
            L"vgc.exe", L"vgtray.exe", L"easyanticheat.exe", L"easyanticheat_eos.exe",
            L"beservice.exe", L"faceitclient.exe", L"gamersclub.exe",

            // Core Windows Infrastructure (Prevents OS collapse)
            L"explorer.exe", L"dwm.exe", L"csrss.exe", L"lsass.exe", L"svchost.exe",
            L"smss.exe", L"winlogon.exe", L"services.exe", L"audiodg.exe", L"spoolsv.exe",
            L"taskhostw.exe", L"wininit.exe", L"sihost.exe", L"fontdrvhost.exe",
            L"system", L"registry", L"conhost.exe",

            // Interactive shell / screenshot / input. These spawn or wake on
            // demand (Win+Shift+S, Start, Search, Settings, typing), so trimming
            // their working set or dropping their pages makes those actions stall
            // hard the first time you use them - which is exactly the "Win+Shift+S
            // lags while OptiMax is on" symptom. They are UI, not background bloat.
            L"screenclippinghost.exe", L"snippingtool.exe", L"screensketch.exe",
            L"shellexperiencehost.exe", L"startmenuexperiencehost.exe",
            L"searchhost.exe", L"searchapp.exe", L"searchui.exe",
            L"textinputhost.exe", L"ctfmon.exe", L"tabtip.exe",
            L"applicationframehost.exe", L"runtimebroker.exe", L"systemsettings.exe",

            // Security stack. Throttling an antivirus does not stop it working -
            // it makes it take LONGER, so a scan overlaps more of the session and
            // is likelier to be aborted and rescheduled. Context: a game
            // stuttering ~hourly was traced to a Defender scheduled scan (Scan
            // Trigger "Scheduled maintenance", Low CPU Priority for Scans
            // DISABLED, ScanAvgCPULoadFactor 50) running 33 s mid-match. Slowing
            // that down is the opposite of the fix.
            //
            // ⚠️ HONEST SCOPE: msmpeng.exe is almost certainly NOT throttled today
            // regardless. Defender runs as a protected process (PPL), and
            // OpenProcess(PROCESS_SET_INFORMATION) against it fails with
            // ERROR_ACCESS_DENIED - verified on this machine - so EnableExtremeMode
            // silently skips it and always has. These entries are therefore
            // defensive and documentary: they state the intent so nobody "fixes"
            // the silent failure later by acquiring more privilege, and they cover
            // the non-PPL members of the set. Do not read this block as a
            // behaviour change; the measured one in this commit is
            // riotclientservices.exe below.
            // smartscreen.exe is here because throttling it stalls every app
            // launch, which reads as the whole machine hanging.
            L"msmpeng.exe", L"mpdefendercoreservice.exe",
            L"securityhealthservice.exe", L"smartscreen.exe",

            // GPU driver hosts. These deliver frames; deprioritising them is
            // self-defeating for a tool whose entire purpose is frame pacing.
            // Deliberately UNCONDITIONAL rather than gated on a vendor flag like
            // the AMD list below: there is no g_IsNvidiaGpu, adding one repeats
            // the failure where g_IsAmdGpu sat permanently false and silently
            // disabled its whole whitelist, and a name that does not exist on
            // this machine simply never matches. Costs nothing to list both.
            L"nvcontainer.exe", L"nvdisplay.container.exe",

            // Game clients that own the LIVE SESSION for a running game -
            // matchmaking, party, and the session heartbeat. Same reasoning as
            // the anti-cheat block at the top: the game process itself is not the
            // only thing that can drop you out of a match.
            // MEASURED, not assumed: riotclientservices.exe was found sitting at
            // BELOW_NORMAL on the dev machine while OptiMax's background
            // throttling was on - i.e. Valorant's own session client was being
            // deprioritised by a tool the user had turned on to help Valorant.
            L"riotclientservices.exe",

            // App Infrastructure
            L"optimax_profiler.exe",

            // Primary Game Engines (Safety net)
            L"valorant-win64-shipping.exe", L"cs2.exe", L"dota2.exe", L"r5apex.exe"
        };

        std::wstring lowerExe = exeName;
        std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(), ::towlower);

        for (const auto& p : protectedList) {
            if (lowerExe == p) return true;
        }

        // --- AMD-SPECIFIC DRIVER WHITELIST ---
        if (g_IsAmdGpu) {
            std::vector<std::wstring> amdList = {
                L"radeonsoftware.exe", L"amddvr.exe", L"amdow.exe",
                L"amdrsserv.exe", L"cpumetricsserver.exe"
            };
            for (const auto& p : amdList) {
                if (lowerExe == p) return true;
            }
        }

        return false;
    }

    // Lowers CPU, disk I/O, and memory page priority for every process not on
    // the protected whitelist. Identifier kept for call-site stability; the
    // user-facing name for this is "Background App Throttling."
    // THE FIX: the safeMode parameter was accepted and then never read - the function
    // body ignored it completely. Both call sites pass the live anti-cheat state
    // (EnableExtremeMode(localAntiCheatActive)), and the whole point of safe mode
    // everywhere else in this codebase is to stay off the undocumented Nt* APIs while
    // an anti-cheat is watching. So the one setting meant to keep users out of trouble
    // did nothing, and NtSetInformationProcess was called against every process on the
    // machine regardless. Safe mode now uses the documented SetPriorityClass only.
    // THE FIX: this used to walk every process and throttle anything not on the
    // protected whitelist, with no idea a game was running. The only games it
    // would not touch were the four hardcoded in the "Primary Game Engines"
    // block, so starting OptiMax while playing ANYTHING else - Fortnite,
    // Overwatch, Rainbow Six, any indie title - dropped that game to
    // BELOW_NORMAL, and outside safe mode to Very Low disk I/O and lowest memory
    // page priority as well. A tool sold as making games faster was measurably
    // making one slower, and it needed no deliberate action to happen: the
    // startup path re-applies this whenever isolation is persisted on.
    //
    // activeGameExe is now skipped, exactly as SmartBackgroundPurge already did
    // (see there - this function was simply never given the same parameter, and
    // that asymmetry was the whole bug). Empty means "no game detected", which
    // restores the old behaviour for an idle machine.
    //
    // Note this is the RIGHT fix rather than adding more names to the whitelist:
    // enumerating game executables only moves the cliff to the next title nobody
    // thought of, and the app already knows which process it is boosting.
    inline void EnableExtremeMode(bool safeMode, const std::wstring& activeGameExe = L"") {
        int ioPrioFailures = 0, memPrioFailures = 0, touchedCount = 0;

        std::wstring lowerGame = activeGameExe;
        std::transform(lowerGame.begin(), lowerGame.end(), lowerGame.begin(), ::towlower);

        ScopedHandle hSnap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    std::wstring exeName = pe.szExeFile;

                    // Never throttle the game the user is actually playing.
                    if (!lowerGame.empty()) {
                        std::wstring lowerExe = exeName;
                        std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(), ::towlower);
                        if (lowerExe == lowerGame) continue;
                    }

                    // Throttle everything except the protected whitelist below
                    if (!IsProtectedProcess(exeName)) {
                        ScopedHandle hP(OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID));
                        if (hP) {
                            touchedCount++;
                            SetPriorityClass(hP, BELOW_NORMAL_PRIORITY_CLASS);
                            if (!safeMode) {
                                ULONG ioPrio = 0; // Very Low I/O
                                if (NtSetInformationProcess(hP, 33, &ioPrio, sizeof(ioPrio)) != STATUS_SUCCESS) ioPrioFailures++;
                                ULONG memPrio = 1; // Very Low Memory Page Priority
                                if (NtSetInformationProcess(hP, 39, &memPrio, sizeof(memPrio)) != STATUS_SUCCESS) memPrioFailures++;
                            }
                        }
                    }
                } while (Process32NextW(hSnap, &pe));
            }
        }

        if (safeMode) {
            SafeAddLog("SAFE MODE: Background apps set to Below Normal CPU priority only - disk and memory priority were left alone while an anti-cheat is running.");
        }
        // THE FIX: these NTSTATUS results used to be discarded entirely. Logged as a
        // single summary rather than per-process to avoid spamming the log when dozens
        // of processes get touched at once.
        else if (ioPrioFailures > 0 || memPrioFailures > 0) {
            SafeAddLog("WARNING: Background throttling could not be applied to " + std::to_string(ioPrioFailures) +
                " process(es) for disk priority and " + std::to_string(memPrioFailures) +
                " process(es) for memory priority, out of " + std::to_string(touchedCount) + " total.");
        }
    }

    inline void RevertOptimizations() {
        SetTimer(false, false);
        Power::RestorePowerPlan();
        OptimizeNetworkLatency(false);
        // Note: background app priorities are restored by RevertIsolationPriorities()
        // in OptiMaxApp.h, not here - this only undoes the per-session system tweaks.
    }
}