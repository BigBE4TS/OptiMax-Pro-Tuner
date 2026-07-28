#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <taskschd.h>
#include <comdef.h>
#include "OptiMaxStateCache.h"

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsuppw.lib")

extern void SafeAddLog(const std::string& msg);

namespace OptiMax {
    namespace Debloat {
        inline void DisableTelemetry() {
            HKEY hKey;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                DWORD val = 0; RegSetValueExW(hKey, L"AllowTelemetry", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
                RegCloseKey(hKey);
            }
        }

        inline void StopBloatServices() {
            SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
            if (hSCManager) {
                // Added dmwappushservice (WAP Push Message Routing) to standard bloat list
                const wchar_t* services[] = { L"DiagTrack", L"SysMain", L"WSearch", L"dmwappushservice" };
                for (auto svc : services) {
                    StateCache::CacheService(svc);

                    SC_HANDLE hService = OpenServiceW(hSCManager, svc, SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_CHANGE_CONFIG);
                    if (hService) {
                        SERVICE_STATUS status;
                        // THE FIX: these results used to be discarded entirely, so a service
                        // that failed to stop or disable did so silently with no indication.
                        BOOL stopOk = ControlService(hService, SERVICE_CONTROL_STOP, &status);
                        BOOL configOk = ChangeServiceConfigW(hService, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
                        if (!stopOk && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
                            SafeAddLog("DEBLOAT WARNING: Failed to stop a bloat service (it may already be in use by another process).");
                        }
                        if (!configOk) {
                            SafeAddLog("DEBLOAT WARNING: Failed to set a bloat service to Disabled (admin rights may be required).");
                        }
                        CloseServiceHandle(hService);
                    }
                    else {
                        SafeAddLog("DEBLOAT WARNING: Could not open a target service - it may not exist on this Windows edition.");
                    }
                }
                CloseServiceHandle(hSCManager);
            }
        }

        inline void RestoreTelemetry() {
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueW(hKey, L"AllowTelemetry");
                RegCloseKey(hKey);
            }
        }

        // The stock Windows start type for each service this app can disable. Used only
        // as a last-resort fallback when nothing was cached (see GetCachedServiceState).
        // THE FIX: the fallback used to be SERVICE_AUTO_START for everything, so
        // restoring dmwappushservice - which ships as Manual - promoted it to Automatic.
        inline DWORD DefaultStartType(const std::wstring& service) {
            if (_wcsicmp(service.c_str(), L"DiagTrack") == 0) return SERVICE_AUTO_START;
            if (_wcsicmp(service.c_str(), L"SysMain") == 0)   return SERVICE_AUTO_START;
            if (_wcsicmp(service.c_str(), L"WSearch") == 0)   return SERVICE_AUTO_START;
            // dmwappushservice and every service in ExtendedServices() ship as Manual.
            return SERVICE_DEMAND_START;
        }

        inline void RestoreBloatServices() {
            SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
            if (hSCManager) {
                const wchar_t* services[] = { L"DiagTrack", L"SysMain", L"WSearch", L"dmwappushservice" };
                for (auto svc : services) {
                    // SERVICE_START and SERVICE_QUERY_STATUS added so a service that was
                    // stopped can actually be started again - see below.
                    SC_HANDLE hService = OpenServiceW(hSCManager, svc, SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_QUERY_STATUS);
                    if (hService) {
                        DWORD originalState = StateCache::GetCachedServiceState(svc, DefaultStartType(svc));
                        if (ChangeServiceConfigW(hService, SERVICE_NO_CHANGE, originalState, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL)) {
                            StateCache::ClearCachedServiceState(svc);

                            // THE FIX: restore used to put the START TYPE back and stop
                            // there. StopBloatServices() stops the service as well as
                            // disabling it, so after "restoring" WSearch the Windows
                            // Search index stayed dead - Start menu search returning
                            // nothing - until the user happened to reboot, with the log
                            // saying the service had been restored. Anything Windows
                            // starts automatically gets started again here; Manual
                            // services are left for their trigger, which is what Manual
                            // means.
                            if (originalState == SERVICE_AUTO_START) {
                                if (!StartServiceW(hService, 0, NULL) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
                                    SafeAddLog("DEBLOAT WARNING: A service's startup type was restored but it could not be started - it will start at the next restart.");
                                }
                            }
                        }
                        else {
                            SafeAddLog("DEBLOAT WARNING: Failed to restore a service's original startup type.");
                        }
                        CloseServiceHandle(hService);
                    }
                    else {
                        SafeAddLog("DEBLOAT WARNING: Could not open a service to restore it.");
                    }
                }
                CloseServiceHandle(hSCManager);
            }
        }

        // ====================================================================
        // Small registry helpers - all reversible via the paths below.
        // ====================================================================
        inline bool SetRegDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD value) {
            HKEY hKey;
            if (RegCreateKeyExW(root, subkey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS) return false;
            LONG res = RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
            RegCloseKey(hKey);
            return res == ERROR_SUCCESS;
        }

        inline bool DelRegValue(HKEY root, const wchar_t* subkey, const wchar_t* name) {
            HKEY hKey;
            if (RegOpenKeyExW(root, subkey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
                // The key not existing at all means the value we would delete is
                // already absent, which is the state the caller wanted.
                return true;
            }
            LONG res = RegDeleteValueW(hKey, name);
            RegCloseKey(hKey);
            return res == ERROR_SUCCESS || res == ERROR_FILE_NOT_FOUND;
        }

        // ====================================================================
        // PRIVACY & TELEMETRY (registry only - no process spawning). Restore
        // deletes the values we wrote, matching RestoreTelemetry's approach:
        // absence of the value = Windows default behaviour.
        // ====================================================================
        struct PrivacyKey { HKEY root; const wchar_t* subkey; const wchar_t* name; DWORD onValue; };

        inline const std::vector<PrivacyKey>& PrivacyKeys() {
            static const std::vector<PrivacyKey> keys = {
                // Advertising ID
                { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", L"Enabled", 0 },
                // Activity history / timeline (machine policy)
                { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"EnableActivityFeed", 0 },
                { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"PublishUserActivities", 0 },
                { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\System", L"UploadUserActivities", 0 },
                // Tailored experiences with diagnostic data
                { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\Privacy", L"TailoredExperiencesWithDiagnosticDataEnabled", 0 },
                // Feedback frequency (never)
                { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Siuf\\Rules", L"NumberOfSIUFInPeriod", 0 },
                // Windows Copilot off
                { HKEY_CURRENT_USER,  L"Software\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", 1 },
                // Start menu / lock screen suggested content
                { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SubscribedContent-338388Enabled", 0 },
                { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager", L"SystemPaneSuggestionsEnabled", 0 },
            };
            return keys;
        }

        // THE FIX: every write here was fire-and-forget and the log reported success
        // unconditionally. Four of these nine values live under HKLM\SOFTWARE\Policies,
        // which needs admin, and on a machine where those writes are denied the user was
        // told activity history and the rest had been turned off when they had not.
        inline void SetPrivacyTweaks(bool enable) {
            int failed = 0, total = 0;
            for (const auto& k : PrivacyKeys()) {
                total++;
                bool ok = enable ? SetRegDword(k.root, k.subkey, k.name, k.onValue)
                                 : DelRegValue(k.root, k.subkey, k.name);
                if (!ok) failed++;
            }

            if (failed == 0) {
                SafeAddLog(enable ? "DEBLOAT: Privacy & telemetry tweaks applied (advertising ID, activity history, tailored experiences, Copilot, suggestions)."
                                  : "DEBLOAT: Privacy & telemetry tweaks reverted to Windows defaults.");
            }
            else {
                SafeAddLog("DEBLOAT WARNING: " + std::to_string(failed) + " of " + std::to_string(total) +
                    " privacy settings could not be changed - administrator rights are required for the machine-wide policy values.");
            }
        }

        // ====================================================================
        // EXTENDED SERVICES - additional low-risk bloat services on top of the
        // core StopBloatServices set. Managed independently so the two toggles
        // don't fight over the same services. Reversible via StateCache.
        // ====================================================================
        inline const std::vector<std::wstring>& ExtendedServices() {
            static const std::vector<std::wstring> svc = {
                L"MapsBroker",      // Downloaded Maps Manager
                L"RetailDemo",      // Retail Demo Service
                L"Fax",             // Fax
                L"WpcMonSvc",       // Parental Controls
                L"WMPNetworkSvc",   // Windows Media Player Network Sharing
                L"lfsvc",           // Geolocation Service
            };
            return svc;
        }

        inline void SetExtendedServices(bool disable) {
            SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
            if (!hSCManager) { SafeAddLog("DEBLOAT WARNING: Could not open Service Control Manager."); return; }
            for (const auto& svc : ExtendedServices()) {
                if (disable) StateCache::CacheService(svc);
                DWORD target = disable ? SERVICE_DISABLED : StateCache::GetCachedServiceState(svc, DefaultStartType(svc));
                SC_HANDLE hService = OpenServiceW(hSCManager, svc.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_CHANGE_CONFIG);
                if (hService) {
                    if (disable) {
                        SERVICE_STATUS status;
                        BOOL stopOk = ControlService(hService, SERVICE_CONTROL_STOP, &status);
                        if (!stopOk && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
                            SafeAddLog("DEBLOAT WARNING: A background service could not be stopped (it may be in use).");
                        }
                    }
                    if (ChangeServiceConfigW(hService, SERVICE_NO_CHANGE, target, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL)) {
                        if (!disable) StateCache::ClearCachedServiceState(svc);
                    }
                    else {
                        SafeAddLog("DEBLOAT WARNING: Could not change a background service's startup type (admin rights may be required).");
                    }
                    CloseServiceHandle(hService);
                }
                // A missing service just means this Windows edition doesn't ship it - silent.
            }
            CloseServiceHandle(hSCManager);
            SafeAddLog(disable ? "DEBLOAT: Extended background services disabled."
                               : "DEBLOAT: Extended background services restored.");
        }

        // ====================================================================
        // TELEMETRY SCHEDULED TASKS - toggled via the native Task Scheduler COM
        // API (no schtasks.exe process spawn, which keeps AV heuristics quiet).
        // ====================================================================
        inline void SetTelemetryTasks(bool disable) {
            const wchar_t* tasks[][2] = {
                { L"\\Microsoft\\Windows\\Customer Experience Improvement Program", L"Consolidator" },
                { L"\\Microsoft\\Windows\\Customer Experience Improvement Program", L"UsbCeip" },
                { L"\\Microsoft\\Windows\\Application Experience", L"Microsoft Compatibility Appraiser" },
                { L"\\Microsoft\\Windows\\Application Experience", L"ProgramDataUpdater" },
                { L"\\Microsoft\\Windows\\Autochk", L"Proxy" },
                { L"\\Microsoft\\Windows\\DiskDiagnostic", L"Microsoft-Windows-DiskDiagnosticDataCollector" },
            };

            // Only CoUninitialize if WE initialised COM on this thread (S_OK). S_FALSE
            // means it was already initialised in a compatible mode; RPC_E_CHANGED_MODE
            // means it's initialised in another mode - either way we must not tear it
            // down under whoever set it up (e.g. the WIC avatar decoder).
            HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

            // THE FIX: put_Enabled's HRESULT was discarded and the log then announced the
            // tasks had been disabled. Several of these live under folders owned by
            // TrustedInstaller, and put_Enabled genuinely returns E_ACCESSDENIED on them
            // even from an elevated process - so on a lot of machines this reported
            // success for tasks it had not touched at all. Count what actually changed.
            int changed = 0, failed = 0, notFound = 0;

            ITaskService* pService = NULL;
            if (SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService)) && pService) {
                if (SUCCEEDED(pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))) {
                    for (auto& t : tasks) {
                        ITaskFolder* pFolder = NULL;
                        if (SUCCEEDED(pService->GetFolder(_bstr_t(t[0]), &pFolder)) && pFolder) {
                            IRegisteredTask* pTask = NULL;
                            if (SUCCEEDED(pFolder->GetTask(_bstr_t(t[1]), &pTask)) && pTask) {
                                if (SUCCEEDED(pTask->put_Enabled(disable ? VARIANT_FALSE : VARIANT_TRUE))) changed++;
                                else failed++;
                                pTask->Release();
                            }
                            else notFound++;
                            pFolder->Release();
                        }
                        else notFound++;
                    }
                }
                pService->Release();

                if (failed == 0) {
                    SafeAddLog(disable ? "DEBLOAT: Telemetry scheduled tasks disabled (" + std::to_string(changed) + " changed)."
                                       : "DEBLOAT: Telemetry scheduled tasks re-enabled (" + std::to_string(changed) + " changed).");
                }
                else {
                    SafeAddLog("DEBLOAT WARNING: " + std::to_string(changed) + " telemetry task(s) changed, " +
                        std::to_string(failed) + " refused by Windows - those are owned by TrustedInstaller and cannot be changed from here.");
                }
            }
            else {
                SafeAddLog("DEBLOAT WARNING: Could not reach the Task Scheduler service.");
            }

            if (hrInit == S_OK) CoUninitialize();
        }

        // ====================================================================
        // WINDOWS 11 "SNAPPIER UI" - lowers the menu-open and mouse-hover delays
        // that Windows ships at 400ms. Both live under HKCU (no admin needed) and
        // are applied live via SystemParametersInfo. The caller gates this on
        // Windows 11 detection. Originals are cached so Restore returns the user's
        // real prior value, not a hardcoded 400.
        // ====================================================================
        inline void SetSnappyUI(bool enable) {
            DWORD origMenu = 400, origHover = 400;
            SystemParametersInfoW(SPI_GETMENUSHOWDELAY, 0, &origMenu, 0);
            SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0, &origHover, 0);

            if (enable) {
                DWORD dummy;
                if (!StateCache::LoadPersistedValue(L"snappy::MenuShowDelay", dummy))
                    StateCache::PersistValue(L"snappy::MenuShowDelay", origMenu);
                if (!StateCache::LoadPersistedValue(L"snappy::MouseHoverTime", dummy))
                    StateCache::PersistValue(L"snappy::MouseHoverTime", origHover);

                // MenuShowDelay value goes in uiParam (2nd arg); MouseHoverTime likewise.
                SystemParametersInfoW(SPI_SETMENUSHOWDELAY, 0,  NULL, SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
                SystemParametersInfoW(SPI_SETMOUSEHOVERTIME, 10, NULL, SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
                SafeAddLog("DEBLOAT: Windows 11 snappy-UI applied (MenuShowDelay 0ms, MouseHoverTime 10ms).");
            }
            else {
                DWORD menu = 400, hover = 400;
                StateCache::LoadPersistedValue(L"snappy::MenuShowDelay", menu);
                StateCache::LoadPersistedValue(L"snappy::MouseHoverTime", hover);
                SystemParametersInfoW(SPI_SETMENUSHOWDELAY, menu,  NULL, SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
                SystemParametersInfoW(SPI_SETMOUSEHOVERTIME, hover, NULL, SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
                StateCache::ClearPersistedValue(L"snappy::MenuShowDelay");
                StateCache::ClearPersistedValue(L"snappy::MouseHoverTime");
                SafeAddLog("DEBLOAT: Windows 11 snappy-UI restored to prior values.");
            }
        }
    }

    // =======================================================================
    // WINDOWS COMPONENT-STORE HEALTH REPAIR (DISM + SFC)
    //
    // The one place in this project that runs an external executable, and the
    // exception is deliberate: there is no public API for `sfc /scannow`, and
    // DismApi.dll cannot drive StartComponentCleanup the way the CLI does.
    //
    // What the rest of the codebase actually avoids is spawning *interpreters* -
    // cmd.exe, powershell.exe, a .bat dropped to disk - because that is the
    // pattern antivirus heuristics score against. Dism.exe and sfc.exe are signed
    // Microsoft servicing binaries, invoked here by absolute path out of the real
    // system directory (never bare names, which would be resolved against PATH)
    // with CREATE_NO_WINDOW and no shell in between.
    //
    // This is slow. ScanHealth is typically 5-15 minutes and RestoreHealth can
    // exceed 30 on a damaged store, so it must never run on the UI thread.
    // =======================================================================
    namespace SystemHealth {

        inline std::atomic<bool> g_running{ false };
        inline std::mutex g_statusMutex;
        inline std::string g_status;
        inline std::string g_lastLine;

        inline void SetStatus(const std::string& s, const std::string& detail = "") {
            std::lock_guard<std::mutex> lk(g_statusMutex);
            g_status = s;
            if (!detail.empty()) g_lastLine = detail;
        }

        inline std::string GetStatus() {
            std::lock_guard<std::mutex> lk(g_statusMutex);
            return g_status;
        }

        inline std::string GetLastLine() {
            std::lock_guard<std::mutex> lk(g_statusMutex);
            return g_lastLine;
        }

        // sfc.exe writes UTF-16LE when its output is redirected; Dism.exe writes
        // 8-bit text. Detected rather than assumed, because guessing wrong turns
        // the entire log into interleaved NULs.
        inline bool LooksUtf16(const std::string& sample) {
            if (sample.size() < 4) return false;
            size_t nulls = 0, n = (std::min)(sample.size(), (size_t)512);
            for (size_t i = 1; i < n; i += 2) if (sample[i] == '\0') nulls++;
            return (nulls * 4 >= n);
        }

        inline std::string Utf16ToUtf8(const std::string& raw) {
            if (raw.size() < 2) return std::string();
            std::wstring w((const wchar_t*)raw.data(), raw.size() / sizeof(wchar_t));
            int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
            if (len <= 0) return std::string();
            std::string out((size_t)len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], len, NULL, NULL);
            return out;
        }

        // Index just past the last line break, measured in RAW BYTES so the
        // leftover partial line can be carried to the next read. Splitting a
        // UTF-16 code unit across two reads would otherwise corrupt it.
        inline size_t LastLineBreakRaw(const std::string& raw, bool utf16) {
            if (!utf16) {
                size_t p = raw.find_last_of('\n');
                return (p == std::string::npos) ? std::string::npos : p + 1;
            }
            if (raw.size() < 2) return std::string::npos;
            for (size_t i = (raw.size() & ~(size_t)1); i >= 2; i -= 2) {
                if (raw[i - 2] == '\n' && raw[i - 1] == '\0') return i;
            }
            return std::string::npos;
        }

        // DISM redraws its progress bar with backspaces and carriage returns, so
        // the raw stream is thousands of "[====  12.3%   ]" fragments. Those are
        // noise in a log file that exists to be read by a human.
        inline bool IsProgressNoise(const std::string& line) {
            if (line.empty()) return true;
            for (char c : line) {
                if (c == '=' || c == '[' || c == ']') continue;
                if (c == ' ' || c == '.' || c == '%' || c == '\t') continue;
                if (c >= '0' && c <= '9') continue;
                return false;   // contains real text, keep it
            }
            // Nothing but bar glyphs, digits and punctuation.
            return true;
        }

        // Runs one tool to completion, streaming its output into the log.
        // Returns false only if the process could not be started at all;
        // exitCodeOut carries the tool's own verdict.
        inline bool RunTool(const std::wstring& exePath, const std::wstring& cmdLine,
            const std::string& label, DWORD& exitCodeOut) {
            exitCodeOut = (DWORD)-1;

            if (GetFileAttributesW(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                SafeAddLog("HEALTH: " + label + " - tool not found on this system, skipped.");
                return false;
            }

            SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
            HANDLE hRead = NULL, hWrite = NULL;
            if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
                SafeAddLog("HEALTH: " + label + " - could not create an output pipe.");
                return false;
            }
            SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW si = { sizeof(si) };
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.hStdOutput = hWrite;
            si.hStdError = hWrite;
            si.wShowWindow = SW_HIDE;

            PROCESS_INFORMATION pi = {};
            std::wstring mutableCmd = cmdLine;   // CreateProcessW may write to this

            BOOL ok = CreateProcessW(exePath.c_str(), &mutableCmd[0], NULL, NULL, TRUE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

            // The parent must drop its copy of the write end, or ReadFile below
            // never sees EOF and this thread hangs for the life of the process.
            CloseHandle(hWrite);

            if (!ok) {
                CloseHandle(hRead);
                SafeAddLog("HEALTH: " + label + " - could not be started (error "
                    + std::to_string(GetLastError()) + "). Administrator rights are required.");
                return false;
            }

            std::string acc;
            char buf[4096];
            DWORD n = 0;
            int logged = 0;
            bool utf16 = false, encodingKnown = false;

            while (ReadFile(hRead, buf, sizeof(buf), &n, NULL) && n > 0) {
                acc.append(buf, n);
                if (!encodingKnown && acc.size() >= 16) { utf16 = LooksUtf16(acc); encodingKnown = true; }

                // Consume only whole lines and keep the remainder for the next
                // read - a tool that emits a long line in two chunks must not
                // lose half of it.
                size_t cut = LastLineBreakRaw(acc, encodingKnown && utf16);
                if (cut == std::string::npos) continue;

                std::string rawChunk = acc.substr(0, cut);
                acc.erase(0, cut);
                std::string chunk = (encodingKnown && utf16) ? Utf16ToUtf8(rawChunk) : rawChunk;

                size_t start = 0;
                while (start <= chunk.size()) {
                    size_t nl = chunk.find('\n', start);
                    std::string line = chunk.substr(start, (nl == std::string::npos ? chunk.size() : nl) - start);
                    for (size_t i = 0; i < line.size(); ++i) if (line[i] == '\r' || line[i] == '\b') line[i] = ' ';
                    while (!line.empty() && line.back() == ' ') line.pop_back();
                    size_t first = line.find_first_not_of(' ');
                    line = (first == std::string::npos) ? "" : line.substr(first);

                    if (!line.empty() && !IsProgressNoise(line)) {
                        SetStatus(label, line);
                        // Capped: a pathological run must not push every other
                        // line out of the 500-entry in-memory log.
                        if (logged < 40) { SafeAddLog("HEALTH [" + label + "]: " + line); logged++; }
                    }
                    if (nl == std::string::npos) break;
                    start = nl + 1;
                }
            }
            CloseHandle(hRead);

            WaitForSingleObject(pi.hProcess, INFINITE);
            if (!GetExitCodeProcess(pi.hProcess, &exitCodeOut)) exitCodeOut = (DWORD)-1;
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }

        // The full sequence. Every step runs even if an earlier one reports a
        // problem: CheckHealth failing is not a reason to skip the repair, and
        // SFC is worth running whether or not DISM succeeded - it repairs from
        // the component store DISM has just been asked to fix, which is exactly
        // why it goes last.
        inline void RunHealthRepair() {
            wchar_t sysDir[MAX_PATH] = {};
            if (!GetSystemDirectoryW(sysDir, MAX_PATH)) {
                SafeAddLog("HEALTH: Could not locate the Windows system directory - aborting.");
                return;
            }
            const std::wstring dism = std::wstring(sysDir) + L"\\Dism.exe";
            const std::wstring sfc = std::wstring(sysDir) + L"\\sfc.exe";

            struct Step {
                const std::wstring* exe;
                std::wstring        cmd;
                const char* label;
            };
            const Step steps[] = {
                { &dism, L"Dism.exe /Online /Cleanup-Image /CheckHealth",           "CheckHealth"           },
                { &dism, L"Dism.exe /Online /Cleanup-Image /ScanHealth",            "ScanHealth"            },
                { &dism, L"Dism.exe /Online /Cleanup-Image /RestoreHealth",         "RestoreHealth"         },
                { &dism, L"Dism.exe /Online /Cleanup-Image /StartComponentCleanup", "StartComponentCleanup" },
                { &sfc,  L"sfc.exe /scannow",                                       "SFC ScanNow"           },
            };

            SafeAddLog("HEALTH: Starting Windows component-store repair. This can take 30+ minutes; "
                "the app stays usable and will log each stage as it finishes.");

            int okCount = 0, failCount = 0, startFail = 0;
            for (const Step& s : steps) {
                SetStatus(s.label, "starting...");
                SafeAddLog(std::string("HEALTH: === ") + s.label + " ===");

                DWORD code = (DWORD)-1;
                if (!RunTool(*s.exe, s.cmd, s.label, code)) { startFail++; continue; }

                if (code == 0) {
                    okCount++;
                    SafeAddLog(std::string("HEALTH: ") + s.label + " completed successfully.");
                }
                else {
                    failCount++;
                    // 3010 is "reboot required", which is a success with a caveat,
                    // not a failure - reporting it as one would be the same lie
                    // this codebase has been full of.
                    if (code == 3010) {
                        SafeAddLog(std::string("HEALTH: ") + s.label
                            + " completed, but a restart is required to finish.");
                    }
                    else {
                        SafeAddLog(std::string("HEALTH: ") + s.label + " reported failure (exit code "
                            + std::to_string((long)code) + ").");
                    }
                }
            }

            std::string summary = "HEALTH: Finished - " + std::to_string(okCount) + " of "
                + std::to_string((int)(sizeof(steps) / sizeof(steps[0]))) + " stages succeeded";
            if (failCount)  summary += ", " + std::to_string(failCount) + " reported a problem";
            if (startFail)  summary += ", " + std::to_string(startFail) + " could not be started";
            summary += ".";
            SafeAddLog(summary);
            SetStatus("Finished", summary);
        }
    }
}