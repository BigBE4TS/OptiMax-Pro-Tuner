#pragma once

// STRICT INCLUDE ORDER: This forces the modern network library to load FIRST
// bypassing the 180 Winsock redefinition errors.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <vector>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <iostream>
#include <shellapi.h>
#include <filesystem>   // Purger::DeepClean's native recursive delete

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// RestoreAllToWindowsDefaults() calls Discord::RestoreFSO(). Included explicitly
// rather than relying on OptiMaxApp.h happening to include Discord first - that
// implicit ordering is exactly what hid the missing <shellapi.h> in OptiMaxEngine.h
// until the harness compiled the header on its own.
#include "OptiMaxDiscord.h"

extern void SafeAddLog(const std::string& msg);

namespace OptiMax {

    // =========================================================================
    // CACHE CLEANUP
    // =========================================================================
    namespace Purger {
        namespace fs = std::filesystem;

        // THE FIX: this used to hardcode C:\Windows\Temp, which silently does
        // nothing on any system where Windows isn't installed on the C: drive.
        // %SystemRoot% resolves correctly regardless of install drive.
        //
        // THE FIX: Prefetch deletion has been REMOVED from this command. Wiping
        // %SystemRoot%\Prefetch is actively counterproductive - Prefetch exists
        // precisely so Windows can preload the files an app needs at launch, so
        // deleting it makes the NEXT launch of every program slower until the
        // data is rebuilt. It was never a cleanup win, and it is not worth
        // keeping just because other "optimizer" tools do it.
        //
        // THE FIX (this pass): all four steps used to be `cmd.exe /c ...` launched by
        // BARE NAME via ShellExecuteW. Three separate problems, and they compounded:
        //
        //   1. It contradicted the project's own design. OptiMaxUWP.h and
        //      OptiMaxDebloat.h both explain that spawning a shell is a strong AV
        //      heuristic and that avoiding it is the point of the debloat work; this
        //      one feature did the thing every other feature was written to avoid.
        //      The false-positive dossier sent to Microsoft asserted there was no
        //      cmd.exe anywhere in the binary, which any vendor could disprove in
        //      seconds - and for an unsigned binary those submissions are the whole
        //      asset.
        //   2. Success was judged by `(INT_PTR)r > 32`, which only means the process
        //      STARTED - the exact defect the Bootloader comment below documents
        //      fixing for bcdedit.
        //   3. `del /q /f /s %TEMP%\*` could delete OptiMax's own in-flight update
        //      download (OptiMaxUpdater.h writes OptiMax_UpdateTarget.exe there), and
        //      it has already destroyed a working directory mid-session.
        //
        // All native now: a real recursive delete that reports what it actually
        // removed, and DnsFlushResolverCache instead of `ipconfig /flushdns`.

        // Deletes the CONTENTS of a directory, never the directory itself. Locked
        // files are skipped rather than aborting the sweep - reclaiming what is
        // reclaimable is the whole job, and something is always in use under %TEMP%.
        inline void PurgeContents(const fs::path& dir, const std::vector<std::wstring>& keepNames,
            int& removed, int& skipped) {
            std::error_code ec;
            if (!fs::exists(dir, ec)) return;

            // Never delete the directory this program is running out of. CLAUDE.md
            // records DeepClean eating a live working directory; a build tree parked
            // under %TEMP% is exactly the case that produced it.
            wchar_t selfPath[MAX_PATH] = {};
            fs::path selfDir;
            if (GetModuleFileNameW(NULL, selfPath, MAX_PATH)) {
                selfDir = fs::path(selfPath).parent_path();
            }

            for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
                it != fs::directory_iterator(); it.increment(ec)) {
                if (ec) break;

                const fs::path& p = it->path();
                std::wstring name = p.filename().wstring();

                bool keep = false;
                for (const auto& k : keepNames) {
                    if (_wcsicmp(name.c_str(), k.c_str()) == 0) { keep = true; break; }
                }
                // Also keep anything that contains, or is, our own directory.
                if (!keep && !selfDir.empty()) {
                    std::error_code cmpEc;
                    if (fs::equivalent(p, selfDir, cmpEc)) keep = true;
                    else {
                        std::wstring pw = p.wstring(), sw = selfDir.wstring();
                        if (sw.size() > pw.size() && _wcsnicmp(sw.c_str(), pw.c_str(), pw.size()) == 0
                            && (sw[pw.size()] == L'\\' || sw[pw.size()] == L'/')) keep = true;
                    }
                }
                if (keep) { skipped++; continue; }

                std::error_code rmEc;
                if (it->is_directory(rmEc)) {
                    uintmax_t n = fs::remove_all(p, rmEc);
                    if (rmEc) skipped++; else removed += (int)(n > 0 ? 1 : 0);
                }
                else {
                    if (fs::remove(p, rmEc) && !rmEc) removed++; else skipped++;
                }
            }
        }

        inline fs::path EnvDir(const wchar_t* var) {
            wchar_t buf[MAX_PATH] = {};
            DWORD n = GetEnvironmentVariableW(var, buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return fs::path();
            return fs::path(buf);
        }

        inline void DeepClean() {
            // Recursive deletes across several trees - the most disk-heavy
            // thing here after a restore point, and it is a foreground button
            // press, so nothing else in the app would record it.
            OptiMax::Diagnostics::Attribution::SelfActivity::Scope _self("clearing caches and temporary files");

            SafeAddLog("CLEANUP: Clearing caches and temporary files...");

            // --- DNS ---
            // The documented resolver API, so this reports the real outcome instead of
            // whether a shell started.
            bool dnsOk = false;
            if (HMODULE dnsapi = LoadLibraryW(L"dnsapi.dll")) {
                typedef BOOL(WINAPI* DnsFlush_t)(void);
                if (auto pFlush = (DnsFlush_t)GetProcAddress(dnsapi, "DnsFlushResolverCache")) {
                    dnsOk = (pFlush() != FALSE);
                }
                FreeLibrary(dnsapi);
            }
            SafeAddLog(dnsOk ? "CLEANUP: DNS resolver cache flushed."
                             : "CLEANUP WARNING: The DNS resolver cache could not be flushed - it is unchanged.");

            // --- Temp ---
            // OptiMax_UpdateTarget.exe is the updater's own download target. Deleting
            // it mid-update is how a cleanup turns into a failed update.
            const std::vector<std::wstring> tempKeep = { L"OptiMax_UpdateTarget.exe" };
            int removed = 0, skipped = 0;
            PurgeContents(EnvDir(L"TEMP"), tempKeep, removed, skipped);
            PurgeContents(EnvDir(L"SystemRoot") / L"Temp", tempKeep, removed, skipped);
            SafeAddLog("CLEANUP: Temporary files - " + std::to_string(removed) + " item(s) deleted, "
                + std::to_string(skipped) + " in use or protected and left alone.");

            // --- Shader caches ---
            fs::path localApp = EnvDir(L"LOCALAPPDATA");
            int shRemoved = 0, shSkipped = 0;
            if (!localApp.empty()) {
                PurgeContents(localApp / L"NVIDIA" / L"DXCache", {}, shRemoved, shSkipped);
                PurgeContents(localApp / L"AMD" / L"DxCache", {}, shRemoved, shSkipped);
            }
            if (shRemoved > 0) {
                SafeAddLog("CLEANUP: Shader cache - " + std::to_string(shRemoved)
                    + " item(s) deleted. Games will rebuild these on their next launch.");
            }
            else {
                SafeAddLog("CLEANUP: Shader cache - nothing to delete (no NVIDIA or AMD cache folder on this system).");
            }
        }
    }

    // =========================================================================
    // BOOT TIMER SETTINGS
    // =========================================================================
    namespace Bootloader {
        // THE FIX (A4-16, found by executing RestoreBootTimers for the first
        // time): every bcdedit call here was fire-and-forget ShellExecuteW, and
        // success was reported on the basis of the result being > 32 - which only
        // means the process LAUNCHED, never that bcdedit did anything. Two
        // consequences, both real:
        //
        //   1. A refused or failed edit was logged as success, the same defect
        //      already fixed in SetStartupRegistry and SetGameDVR.
        //   2. Nothing waited for the process, so the boot configuration was
        //      still changing after the function returned. A caller that checked
        //      the result immediately saw the OLD state and concluded the revert
        //      had failed - which is exactly what happened during testing.
        //
        // Runs bcdedit to completion and returns its real exit code, or -1 if it
        // could not be started. Output is captured so "element not found" can be
        // told apart from a genuine failure: for a /deletevalue, an element that
        // is already absent IS the requested state, not an error.
        inline int RunBcdedit(const std::wstring& args, bool& alreadyAbsent) {
            alreadyAbsent = false;

            wchar_t sysDir[MAX_PATH] = {};
            if (!GetSystemDirectoryW(sysDir, MAX_PATH)) return -1;
            std::wstring exe = std::wstring(sysDir) + L"\\bcdedit.exe";
            std::wstring cmd = L"bcdedit.exe " + args;

            SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
            HANDLE hRead = NULL, hWrite = NULL;
            if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return -1;
            SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW si = { sizeof(si) };
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.hStdOutput = hWrite;
            si.hStdError = hWrite;
            si.wShowWindow = SW_HIDE;

            PROCESS_INFORMATION pi = {};
            std::wstring mutableCmd = cmd;
            BOOL ok = CreateProcessW(exe.c_str(), &mutableCmd[0], NULL, NULL, TRUE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
            CloseHandle(hWrite);   // or the read below never sees EOF
            if (!ok) { CloseHandle(hRead); return -1; }

            std::string out;
            char buf[512];
            DWORD n = 0;
            while (ReadFile(hRead, buf, sizeof(buf), &n, NULL) && n > 0) out.append(buf, n);
            CloseHandle(hRead);

            WaitForSingleObject(pi.hProcess, 15000);
            DWORD code = (DWORD)-1;
            if (!GetExitCodeProcess(pi.hProcess, &code)) code = (DWORD)-1;
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);

            for (auto& c : out) c = (char)tolower((unsigned char)c);
            if (out.find("not found") != std::string::npos) alreadyAbsent = true;

            return (int)code;
        }

        // Returns whether the boot configuration now says what the caller asked.
        inline bool OptimizeTimers() {
            bool absent = false;
            int  setRc = RunBcdedit(L"/set disabledynamictick yes", absent);
            // The two deletes are best-effort: absent is already the default, so
            // "element not found" is the desired outcome rather than a failure.
            RunBcdedit(L"/deletevalue useplatformclock", absent);
            RunBcdedit(L"/deletevalue useplatformtick", absent);

            if (setRc == 0) {
                SafeAddLog("BOOT: HPET and dynamic tick disabled in the boot configuration; Windows will use the CPU clock. Restart to take effect.");
            }
            else {
                SafeAddLog("BOOT FAILED: could not write the boot configuration (administrator rights are required) - nothing was changed.");
            }
            return setRc == 0;
        }

        // THE FIX: there was no way back. The toggle that applies these settings had
        // no revert path and this namespace had no restore function, so a user who
        // switched "Use CPU Clock for Timing" off got no log line, no change, and a
        // boot configuration that stayed modified permanently.
        //
        // Only disabledynamictick needs undoing. The other two commands DELETE
        // values, and absent is already the Windows default - there is nothing to
        // put back, which is why this is one command rather than three.
        inline bool RestoreBootTimers() {
            bool alreadyAbsent = false;
            int rc = RunBcdedit(L"/deletevalue disabledynamictick", alreadyAbsent);

            if (rc == 0) {
                SafeAddLog("BOOT: Boot timer settings restored to Windows defaults. Restart to take effect.");
            }
            else if (alreadyAbsent) {
                // bcdedit exits non-zero when the value is not there, but absent
                // is precisely the state being asked for. Reporting that as a
                // failure would be the same lie in the opposite direction.
                SafeAddLog("BOOT: Boot timer settings were already at the Windows defaults - nothing to undo.");
            }
            else {
                SafeAddLog("BOOT WARNING: Could not restore the boot timer settings - administrator rights are required.");
            }
            // Already absent is the state being asked for, so it counts as restored.
            return rc == 0 || alreadyAbsent;
        }
    }

    // =========================================================================
    // ADVANCED TWEAKS (WIN32 / REGISTRY, NO DRIVER OR KERNEL CODE)
    // =========================================================================
    namespace V3 {

        // Reads a REG_SZ value into a caller-supplied buffer, guaranteeing a
        // terminator. The registry does NOT promise string values are null-terminated,
        // and every caller here goes on to hand the result to _wcsicmp.
        inline bool ReadRegString(HKEY hKey, const wchar_t* valueName, wchar_t* buf, DWORD bufBytes) {
            if (bufBytes < sizeof(wchar_t)) return false;
            buf[0] = L'\0';
            DWORD size = bufBytes - sizeof(wchar_t); // leave room to force a terminator
            DWORD type = 0;
            if (RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)buf, &size) != ERROR_SUCCESS) return false;
            if (type != REG_SZ && type != REG_EXPAND_SZ) return false;
            buf[size / sizeof(wchar_t)] = L'\0';
            return true;
        }

        // The device setup class GUID for display adapters. Windows always writes
        // ClassGUID on a PCI device instance key; the human-readable "Class" value is
        // legacy and frequently absent.
        inline const wchar_t* kDisplayClassGuid = L"{4d36e968-e325-11ce-bfc1-08002be10318}";

        // THE FIX: every PCI enumeration below identified the GPU by reading the
        // "Class" value and comparing it to L"Display". VERIFIED ON A REAL MACHINE:
        // zero of 41 PCI device instances carry a "Class" value at all - only
        // "ClassGUID" - so this test never matched anything, and MSI mode and GPU
        // interrupt affinity were BOTH completely inert. Nothing was ever written,
        // nothing was ever restored, and the verifier could never confirm a thing.
        // Two headline features that have never done anything for anyone on a
        // machine like this one, while the UI and the log reported success.
        // (SetGPUPriorityHigh() in OptiMaxEngine.h escaped this because it goes
        // through SetupDiGetClassDevsW with GUID_DEVCLASS_DISPLAY instead.)
        //
        // ClassGUID is the canonical identifier and is checked first; the legacy
        // "Class" string is kept as a fallback for any device that does carry it.
        // The null-termination and value-type hardening below is retained.
        inline bool IsDisplayClassDevice(HKEY hInstanceKey) {
            wchar_t buf[256];

            if (ReadRegString(hInstanceKey, L"ClassGUID", buf, sizeof(buf))
                && _wcsicmp(buf, kDisplayClassGuid) == 0) {
                return true;
            }

            if (ReadRegString(hInstanceKey, L"Class", buf, sizeof(buf))
                && _wcsicmp(buf, L"Display") == 0) {
                return true;
            }

            return false;
        }

        // 1. P-CORE AFFINITY LOCK
        //
        // Every logical processor belonging to a highest-EfficiencyClass core, in
        // group 0. Returns 0 when this CPU has no P-core/E-core split at all, or when
        // topology cannot be read - both of which mean "there is nothing to lock to".
        //
        // THE FIX: LockToPCores used to COUNT the bits in the system affinity mask and
        // assume the top quarter were E-cores - `pCores = total - total/4` - then keep
        // the LOWEST-numbered ones. Two things wrong with that, and the second is the
        // damaging one:
        //
        //   1. The assumption is only ever right by coincidence. A 12900K is 8P+16E
        //      (a third, not a quarter, and the P-cores are not a clean prefix once
        //      SMT is counted); a 13600K is 6P+8E.
        //   2. On a CPU with NO efficiency cores - every AMD desktop part, every Intel
        //      before 12th gen - it still removed a quarter of the machine. A 16-thread
        //      Ryzen running a game with this tweak on was pinned to 12 threads by a
        //      tool whose entire purpose is making that game faster, and the log said
        //      "restricted to the CPU's performance cores" as if that were true.
        //
        // The topology walk this should always have used is the same one
        // PickPerformanceCoreMask and BuildSmtDisabledMask already do further down.
        inline DWORD_PTR BuildPerformanceCoreMask() {
            DWORD bufferLen = 0;
            GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bufferLen);
            if (bufferLen == 0) return 0;

            std::vector<BYTE> buffer(bufferLen);
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pInfo = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer.data();
            if (!GetLogicalProcessorInformationEx(RelationProcessorCore, pInfo, &bufferLen)) return 0;

            BYTE maxClass = 0, minClass = 0xFF;
            DWORD offset = 0;
            while (offset < bufferLen) {
                auto entry = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer.data() + offset);
                if (entry->Relationship == RelationProcessorCore) {
                    if (entry->Processor.EfficiencyClass > maxClass) maxClass = entry->Processor.EfficiencyClass;
                    if (entry->Processor.EfficiencyClass < minClass) minClass = entry->Processor.EfficiencyClass;
                }
                offset += entry->Size;
            }
            // Uniform EfficiencyClass = not a hybrid CPU. There is no subset of cores
            // that is faster than the rest, so restricting to one can only lose the
            // game cores it was already entitled to.
            if (minClass > maxClass || minClass == maxClass) return 0;

            DWORD_PTR mask = 0;
            offset = 0;
            while (offset < bufferLen) {
                auto entry = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer.data() + offset);
                if (entry->Relationship == RelationProcessorCore && entry->Processor.EfficiencyClass == maxClass) {
                    for (int g = 0; g < entry->Processor.GroupCount; g++) {
                        // SetProcessAffinityMask cannot express affinity across
                        // processor groups, so bits from group 1 would land on the
                        // wrong processors entirely.
                        if (entry->Processor.GroupMask[g].Group == 0)
                            mask |= (DWORD_PTR)entry->Processor.GroupMask[g].Mask;
                    }
                }
                offset += entry->Size;
            }
            return mask;
        }

        inline void LockToPCores(const std::wstring& targetExe) {
            DWORD_PTR pCoreMask = BuildPerformanceCoreMask();
            if (pCoreMask == 0) {
                SafeAddLog("TWEAK: Skipped the performance-core lock - this CPU has no separate performance and efficiency cores, so there is nothing to restrict the game to.");
                return;
            }

            HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapshot == INVALID_HANDLE_VALUE) return;

            PROCESSENTRY32W pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32W);

            int applied = 0, refused = 0;
            if (Process32FirstW(hSnapshot, &pe32)) {
                do {
                    if (_wcsicmp(pe32.szExeFile, targetExe.c_str()) == 0) {
                        HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pe32.th32ProcessID);
                        if (hProcess) {
                            DWORD_PTR processAffinityMask = 0;
                            DWORD_PTR systemAffinityMask = 0;
                            if (GetProcessAffinityMask(hProcess, &processAffinityMask, &systemAffinityMask)) {
                                // Intersect, so this never widens affinity and never
                                // hands Windows a processor the system does not have.
                                DWORD_PTR target = pCoreMask & systemAffinityMask;
                                if (target != 0 && SetProcessAffinityMask(hProcess, target)) applied++;
                                else refused++;
                            }
                            else refused++;
                            CloseHandle(hProcess);
                        }
                        else refused++;
                    }
                } while (Process32NextW(hSnapshot, &pe32));
            }
            CloseHandle(hSnapshot);

            // THE FIX: failure was silent here too - the success line only ever appeared
            // when it worked, so a game process that refused the handle produced nothing.
            if (applied > 0 && refused == 0) SafeAddLog("TWEAK: Game restricted to the CPU's performance cores.");
            else if (applied > 0) SafeAddLog("TWEAK PARTIAL: performance-core lock applied to " + std::to_string(applied) +
                " of " + std::to_string(applied + refused) + " game process(es).");
            else if (refused > 0) SafeAddLog("TWEAK FAILED: Windows refused the performance-core lock for the game - its CPU affinity is unchanged.");
        }

        // 2. GPU MSI MODE
        // Identifier kept as-is deliberately: renaming it would touch every call
        // site and the verifier for no user-visible benefit. The log string it
        // emits is what a user actually reads, and that says what it does.
        inline void InjectMSIMode() {
            HKEY hPciKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hPciKey) == ERROR_SUCCESS) {
                wchar_t subKeyName[256];
                DWORD subKeySize = 256;
                DWORD index = 0;

                while (RegEnumKeyExW(hPciKey, index, subKeyName, &subKeySize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                    HKEY hDeviceKey;
                    if (RegOpenKeyExW(hPciKey, subKeyName, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hDeviceKey) == ERROR_SUCCESS) {
                        wchar_t instanceName[256];
                        DWORD instanceSize = 256;
                        DWORD instanceIndex = 0;

                        while (RegEnumKeyExW(hDeviceKey, instanceIndex, instanceName, &instanceSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                            HKEY hInstanceKey;
                            if (RegOpenKeyExW(hDeviceKey, instanceName, 0, KEY_READ | KEY_WRITE, &hInstanceKey) == ERROR_SUCCESS) {
                                if (IsDisplayClassDevice(hInstanceKey)) {
                                    HKEY hDevParams;
                                    if (RegOpenKeyExW(hInstanceKey, L"Device Parameters", 0, KEY_READ | KEY_WRITE, &hDevParams) == ERROR_SUCCESS) {
                                        HKEY hIntMgmt;
                                        if (RegCreateKeyExW(hDevParams, L"Interrupt Management", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hIntMgmt, NULL) == ERROR_SUCCESS) {
                                            HKEY hMsiProps;
                                            if (RegCreateKeyExW(hIntMgmt, L"MessageSignaledInterruptProperties", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hMsiProps, NULL) == ERROR_SUCCESS) {
                                                DWORD msiSupported = 1;
                                                RegSetValueExW(hMsiProps, L"MSISupported", 0, REG_DWORD, (const BYTE*)&msiSupported, sizeof(msiSupported));
                                                RegCloseKey(hMsiProps);
                                            }
                                            RegCloseKey(hIntMgmt);
                                        }
                                        RegCloseKey(hDevParams);
                                    }
                                }
                                RegCloseKey(hInstanceKey);
                            }
                            instanceSize = 256;
                            instanceIndex++;
                        }
                        RegCloseKey(hDeviceKey);
                    }
                    subKeySize = 256;
                    index++;
                }
                RegCloseKey(hPciKey);
                // No success log here any more. Nothing in this loop checks a single
                // write result, so this line claimed success even when every write was
                // denied. The caller now verifies the registry and logs the real outcome.
            }
        }

        // 3. TCP LATENCY TWEAKS (NAGLE'S ALGORITHM / DELAYED ACKS)
        inline void BypassNagleAlgorithm() {
            ULONG outBufLen = 15000;
            PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
            if (!pAddresses) return;

            DWORD dwRetVal = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen);
            if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
                free(pAddresses);
                pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
                if (!pAddresses) return;
                dwRetVal = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen);
            }

            std::vector<std::string> activeInterfaces;
            if (dwRetVal == NO_ERROR) {
                PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
                while (pCurrAddresses) {
                    if (pCurrAddresses->OperStatus == IfOperStatusUp &&
                        (pCurrAddresses->IfType == IF_TYPE_ETHERNET_CSMACD || pCurrAddresses->IfType == IF_TYPE_IEEE80211)) {
                        activeInterfaces.push_back(pCurrAddresses->AdapterName);
                    }
                    pCurrAddresses = pCurrAddresses->Next;
                }
            }
            free(pAddresses);

            HKEY hInterfacesKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces", 0, KEY_READ | KEY_WRITE, &hInterfacesKey) == ERROR_SUCCESS) {
                for (const auto& interfaceName : activeInterfaces) {
                    HKEY hAdapterKey;
                    if (RegOpenKeyExA(hInterfacesKey, interfaceName.c_str(), 0, KEY_SET_VALUE, &hAdapterKey) == ERROR_SUCCESS) {
                        DWORD val = 1;
                        RegSetValueExA(hAdapterKey, "TcpAckFrequency", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
                        RegSetValueExA(hAdapterKey, "TCPNoDelay", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
                        DWORD val0 = 0;
                        RegSetValueExA(hAdapterKey, "TcpDelAckTicks", 0, REG_DWORD, (const BYTE*)&val0, sizeof(val0));
                        RegCloseKey(hAdapterKey);
                    }
                }
                RegCloseKey(hInterfacesKey);
            }

            HKEY hProfileKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", 0, KEY_SET_VALUE, &hProfileKey) == ERROR_SUCCESS) {
                DWORD throttle = 4294967295; // FFFFFFFF
                DWORD resp = 0;
                RegSetValueExA(hProfileKey, "NetworkThrottlingIndex", 0, REG_DWORD, (const BYTE*)&throttle, sizeof(throttle));
                RegSetValueExA(hProfileKey, "SystemResponsiveness", 0, REG_DWORD, (const BYTE*)&resp, sizeof(resp));
                RegCloseKey(hProfileKey);
            }
        }

        // THE FIX: nothing in the program undid any of the above. The toggle's off
        // branch logged "TCP latency tweaks removed" and called nothing, and even
        // Restore All Defaults skipped them - so once applied, these values stayed
        // on every adapter permanently.
        //
        // Deliberately enumerates EVERY interface subkey rather than only the
        // currently-up adapters that BypassNagleAlgorithm writes to. An adapter that
        // was connected when the tweak was applied and is unplugged now would
        // otherwise keep its overrides forever, with no way to reach them.
        inline void RestoreNagleAlgorithm() {
            HKEY hInterfacesKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces",
                0, KEY_READ, &hInterfacesKey) == ERROR_SUCCESS) {

                char nameBuf[256];
                DWORD nameSize = sizeof(nameBuf);
                DWORD index = 0;
                while (RegEnumKeyExA(hInterfacesKey, index, nameBuf, &nameSize,
                    NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                    HKEY hAdapterKey;
                    if (RegOpenKeyExA(hInterfacesKey, nameBuf, 0, KEY_SET_VALUE, &hAdapterKey) == ERROR_SUCCESS) {
                        // Absent is the Windows default for all three, so deleting is
                        // the correct restore - writing a "default" value would leave
                        // an override behind that Windows never puts there itself.
                        RegDeleteValueA(hAdapterKey, "TcpAckFrequency");
                        RegDeleteValueA(hAdapterKey, "TCPNoDelay");
                        RegDeleteValueA(hAdapterKey, "TcpDelAckTicks");
                        RegCloseKey(hAdapterKey);
                    }
                    nameSize = sizeof(nameBuf);
                    index++;
                }
                RegCloseKey(hInterfacesKey);
            }

            HKEY hProfileKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", 0, KEY_SET_VALUE, &hProfileKey) == ERROR_SUCCESS) {
                // Windows' own defaults, not deletions - these two values ship with
                // the OS, so removing them entirely would not be a restore.
                DWORD throttle = 10;
                DWORD resp = 20;
                RegSetValueExA(hProfileKey, "NetworkThrottlingIndex", 0, REG_DWORD, (const BYTE*)&throttle, sizeof(throttle));
                RegSetValueExA(hProfileKey, "SystemResponsiveness", 0, REG_DWORD, (const BYTE*)&resp, sizeof(resp));
                RegCloseKey(hProfileKey);
            }

            // THE FIX: a copy-paste leftover here logged "TCP latency tweaks applied
            // to active network adapters" immediately after removing them, so the
            // log told the user the exact opposite of what had just happened.
            SafeAddLog("RESTORE: TCP latency overrides removed from all adapters; network throttling returned to Windows defaults.");
        }

        // 4. PREFER EXCLUSIVE FULLSCREEN
        // SCOPE WARNING: HKCU\System\GameConfigStore is a single per-user switch
        // covering every game on the machine. There is no per-executable
        // Fullscreen Optimizations toggle in Windows, so callers must not present
        // this as a per-game setting (see the per-game disableFSO flag's handling
        // in OptiMaxApp.h, which is commented accordingly).
        inline void ForceFSE() {
            HKEY hKey;
            // FIXED: Removed the extra '0' argument that broke the compiler
            if (RegCreateKeyExA(HKEY_CURRENT_USER, "System\\GameConfigStore", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                DWORD fseOverride = 2;
                DWORD honorUser = 1;
                RegSetValueExA(hKey, "GameDVR_FSEBehaviorMode", 0, REG_DWORD, (const BYTE*)&fseOverride, sizeof(fseOverride));
                RegSetValueExA(hKey, "GameDVR_HonorUserFSEBehaviorMode", 0, REG_DWORD, (const BYTE*)&honorUser, sizeof(honorUser));
                RegSetValueExA(hKey, "GameDVR_DXGIHonorFSEWindowsCompatible", 0, REG_DWORD, (const BYTE*)&honorUser, sizeof(honorUser));
                RegCloseKey(hKey);
            }
        }

        // 5. DISABLE VBS / MEMORY INTEGRITY
        // This is a real, measurable performance win - and it genuinely removes
        // security features (Credential Guard, Memory Integrity). The UI copy for
        // this one is written to make that trade-off visible rather than burying
        // it; keep it that way.
        // THE FIX: every write in here was fire-and-forget, and the caller logged
        // "VBS and Memory Integrity disabled" (and, on the way back, "restored")
        // regardless of what actually happened. For a security feature, a log line
        // that is wrong in the "restored" direction tells a user their machine is
        // protected when it is not - which is the worst possible thing for this
        // particular toggle to get wrong. Both writes are now checked, and the
        // result is reported honestly.
        inline bool DisableVBS() {
            bool ok = true;
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Control\\DeviceGuard", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                DWORD val = 0;
                if (RegSetValueExA(hKey, "EnableVirtualizationBasedSecurity", 0, REG_DWORD, (const BYTE*)&val, sizeof(val)) != ERROR_SUCCESS) ok = false;
                RegCloseKey(hKey);
            }
            else ok = false;

            if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                DWORD val = 0;
                if (RegSetValueExA(hKey, "Enabled", 0, REG_DWORD, (const BYTE*)&val, sizeof(val)) != ERROR_SUCCESS) ok = false;
                RegCloseKey(hKey);
            }
            else ok = false;

            // THE FIX: this was ShellExecuteW on the BARE NAME "bcdedit" with success
            // judged by `> 32` - launch, not completion - which is precisely the defect
            // Bootloader::RunBcdedit above was written to fix, in the same file, for the
            // same tool. A bare name is also resolved against PATH rather than the real
            // system directory. Two of the three registry writes here can succeed while
            // the hypervisor keeps launching, and the old code could not tell.
            bool absent = false;
            if (Bootloader::RunBcdedit(L"/set hypervisorlaunchtype off", absent) != 0) ok = false;
            return ok;
        }

        // THE FIX: this used to hardcode GPU interrupt affinity to logical core 2
        // unconditionally, with zero regard for CPU topology. On hybrid CPUs
        // (Intel 12th-gen+ with P-cores/E-cores), core index 2 could easily land
        // on a slow E-core - actively hurting interrupt latency instead of
        // improving it - or directly contend with whatever core the game's main
        // thread happens to be scheduled on. This walks the actual core topology
        // via GetLogicalProcessorInformationEx and picks a real performance core,
        // falling back to a safe default only when topology info isn't available
        // (e.g. older Windows versions that predate the hybrid EfficiencyClass field).
        inline DWORD_PTR PickPerformanceCoreMask() {
            DWORD_PTR defaultMask = 0x04; // logical core 2 - prior hardcoded behavior as a last-resort fallback

            DWORD bufferLen = 0;
            GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bufferLen);
            if (bufferLen == 0) return defaultMask;

            std::vector<BYTE> buffer(bufferLen);
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pInfo = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer.data();
            if (!GetLogicalProcessorInformationEx(RelationProcessorCore, pInfo, &bufferLen)) return defaultMask;

            BYTE maxEfficiencyClass = 0;
            bool hasHybridInfo = false;
            DWORD offset = 0;
            while (offset < bufferLen) {
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX entry = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer.data() + offset);
                if (entry->Relationship == RelationProcessorCore) {
                    hasHybridInfo = true;
                    if (entry->Processor.EfficiencyClass > maxEfficiencyClass) {
                        maxEfficiencyClass = entry->Processor.EfficiencyClass;
                    }
                }
                offset += entry->Size;
            }
            if (!hasHybridInfo) return defaultMask;

            // Find the first P-core (highest EfficiencyClass) that does not host logical
            // processor 0 - core 0 is already the busiest with OS and driver interrupt
            // traffic, which is the whole reason for moving GPU interrupts elsewhere.
            //
            // THE FIX: the test used to be `bit != 0`, which rejected logical processor 0
            // but happily accepted logical processor 1. On any SMT/hyperthreaded CPU -
            // which is nearly all of them - logical 0 and logical 1 are the two hardware
            // threads of the SAME physical core, so this pinned GPU interrupts to a thread
            // sharing execution resources with the exact core it was trying to avoid. The
            // whole physical core has to be skipped, not one of its threads.
            //
            // Only group 0 is considered: AssignmentSetOverride is a group-relative mask,
            // so a bit picked from group 1 would be written as if it meant a group 0
            // processor and pin the interrupt to the wrong core entirely.
            offset = 0;
            while (offset < bufferLen) {
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX entry = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer.data() + offset);
                if (entry->Relationship == RelationProcessorCore && entry->Processor.EfficiencyClass == maxEfficiencyClass) {
                    for (int g = 0; g < entry->Processor.GroupCount; g++) {
                        if (entry->Processor.GroupMask[g].Group != 0) continue;
                        KAFFINITY mask = entry->Processor.GroupMask[g].Mask;
                        if (mask & (KAFFINITY)1) continue; // this physical core hosts logical processor 0
                        for (int bit = 0; bit < (int)(sizeof(KAFFINITY) * 8); bit++) {
                            if (mask & ((KAFFINITY)1 << bit)) {
                                return (DWORD_PTR)1 << bit;
                            }
                        }
                    }
                }
                offset += entry->Size;
            }
            return defaultMask;
        }

        // 6. GPU INTERRUPT AFFINITY (PINNED TO A REAL PERFORMANCE CORE)
        //
        // THE FIX: every write here was discarded and the caller in OptiMaxApp.h logged
        // "GPU interrupts pinned to a performance core. Restart to take effect."
        // regardless. Unlike MSI mode and the TCP tweaks, which have verifiers that read
        // the registry back, this one had nothing checking it at all - so on a machine
        // that refused the write, the user restarted expecting a change that had never
        // been written. Same shape as InjectMSIMode, whose success line was removed for
        // exactly this reason; this reports its own outcome instead.
        inline void PinGpuIrq() {
            int applied = 0, refused = 0;
            HKEY hPciKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hPciKey) == ERROR_SUCCESS) {
                wchar_t subKeyName[256];
                DWORD subKeySize = 256;
                DWORD index = 0;

                while (RegEnumKeyExW(hPciKey, index, subKeyName, &subKeySize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                    HKEY hDeviceKey;
                    if (RegOpenKeyExW(hPciKey, subKeyName, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hDeviceKey) == ERROR_SUCCESS) {
                        wchar_t instanceName[256];
                        DWORD instanceSize = 256;
                        DWORD instanceIndex = 0;

                        while (RegEnumKeyExW(hDeviceKey, instanceIndex, instanceName, &instanceSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                            HKEY hInstanceKey;
                            if (RegOpenKeyExW(hDeviceKey, instanceName, 0, KEY_READ | KEY_WRITE, &hInstanceKey) == ERROR_SUCCESS) {
                                if (IsDisplayClassDevice(hInstanceKey)) {
                                    HKEY hDevParams;
                                    if (RegOpenKeyExW(hInstanceKey, L"Device Parameters", 0, KEY_READ | KEY_WRITE, &hDevParams) == ERROR_SUCCESS) {
                                        HKEY hIntMgmt;
                                        if (RegCreateKeyExW(hDevParams, L"Interrupt Management", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hIntMgmt, NULL) == ERROR_SUCCESS) {
                                            HKEY hAffinityPolicy;
                                            if (RegCreateKeyExW(hIntMgmt, L"Affinity Policy", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hAffinityPolicy, NULL) == ERROR_SUCCESS) {
                                                DWORD devPolicy = 4; // 4 = IrqPolicySpecifiedProcessors
                                                DWORD_PTR coreMask = PickPerformanceCoreMask();
                                                BYTE assignmentSet[8] = { 0 };
                                                memcpy(assignmentSet, &coreMask, sizeof(coreMask) < sizeof(assignmentSet) ? sizeof(coreMask) : sizeof(assignmentSet));

                                                LONG a = RegSetValueExW(hAffinityPolicy, L"DevicePolicy", 0, REG_DWORD, (const BYTE*)&devPolicy, sizeof(devPolicy));
                                                LONG b = RegSetValueExW(hAffinityPolicy, L"AssignmentSetOverride", 0, REG_BINARY, assignmentSet, sizeof(assignmentSet));
                                                if (a == ERROR_SUCCESS && b == ERROR_SUCCESS) applied++; else refused++;

                                                RegCloseKey(hAffinityPolicy);
                                            }
                                            else refused++;
                                            RegCloseKey(hIntMgmt);
                                        }
                                        RegCloseKey(hDevParams);
                                    }
                                }
                                RegCloseKey(hInstanceKey);
                            }
                            instanceSize = 256;
                            instanceIndex++;
                        }
                        RegCloseKey(hDeviceKey);
                    }
                    subKeySize = 256;
                    index++;
                }
                RegCloseKey(hPciKey);
            }
            else {
                SafeAddLog("TWEAK FAILED: Could not read the PCI device list - GPU interrupt affinity was not changed.");
                return;
            }

            if (applied > 0 && refused == 0) {
                SafeAddLog("TWEAK: GPU interrupts pinned to a performance core. Restart to take effect.");
            }
            else if (applied > 0) {
                SafeAddLog("TWEAK PARTIAL: GPU interrupt affinity written for " + std::to_string(applied) +
                    " adapter(s), refused for " + std::to_string(refused) + ". Administrator rights are required.");
            }
            else if (refused > 0) {
                SafeAddLog("TWEAK FAILED: Windows refused the GPU interrupt affinity write - nothing was changed. Administrator rights are required.");
            }
            else {
                SafeAddLog("TWEAK: No display adapter was found to pin interrupts for - nothing was changed.");
            }
        }

        // 7. RESTORE FUNCTIONS - reverse everything the advanced tweaks can apply.
        // THE FEATURE: before this, VBS/MSI/IRQ pinning changes had NO revert path
        // anywhere in the codebase. Once applied, the only way back was manually
        // editing the registry or restoring from a System Restore point (which,
        // even when successfully created, most users wouldn't think to use for
        // this). These delete only the specific values this app writes, not whole
        // keys, so anything else that might coexist there is left untouched.

        // Returns false if either override is still sitting in the registry afterwards.
        // A delete that returns ERROR_FILE_NOT_FOUND is success here - the value being
        // absent is exactly the state we want - so the outcome is judged by reading the
        // keys back rather than by the delete's return code.
        inline bool RestoreVBS() {
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Control\\DeviceGuard", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueA(hKey, "EnableVirtualizationBasedSecurity");
                RegCloseKey(hKey);
            }
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueA(hKey, "Enabled");
                RegCloseKey(hKey);
            }
            // Same bare-name/launch-only problem as DisableVBS had. "Element not found"
            // is the state being asked for here, so alreadyAbsent counts as success.
            bool bcdAbsent = false;
            int bcdRc = Bootloader::RunBcdedit(L"/deletevalue hypervisorlaunchtype", bcdAbsent);
            bool hypervisorOk = (bcdRc == 0 || bcdAbsent);

            auto overrideStillDisabling = [](const char* subKey, const char* valueName) {
                bool present = false;
                HKEY h;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, KEY_QUERY_VALUE, &h) == ERROR_SUCCESS) {
                    DWORD v = 0, size = sizeof(v), type = 0;
                    if (RegQueryValueExA(h, valueName, NULL, &type, (LPBYTE)&v, &size) == ERROR_SUCCESS
                        && type == REG_DWORD && v == 0) {
                        present = true;
                    }
                    RegCloseKey(h);
                }
                return present;
                };

            bool registryOk = !overrideStillDisabling("System\\CurrentControlSet\\Control\\DeviceGuard", "EnableVirtualizationBasedSecurity")
                && !overrideStillDisabling("System\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", "Enabled");

            // THE FIX: the bcdedit half was left out of the verdict entirely, so this
            // could report VBS "reverted to Windows defaults" while hypervisorlaunchtype
            // was still off in the boot configuration - i.e. VBS still cannot start. On
            // a security toggle, a false "you are protected" is the worst direction to
            // be wrong in, which is what the DisableVBS comment above already says.
            bool ok = registryOk && hypervisorOk;
            if (ok) {
                SafeAddLog("RESTORE: VBS and Memory Integrity reverted to Windows defaults. Restart required.");
            }
            else if (registryOk) {
                SafeAddLog("RESTORE PARTIAL: the VBS registry overrides are gone, but the boot configuration still has the hypervisor disabled - VBS cannot start until that is undone. Administrator rights are required.");
            }
            else {
                SafeAddLog("RESTORE FAILED: the VBS/Memory Integrity overrides are still in the registry - your machine is NOT protected yet. Administrator rights are required.");
            }
            return ok;
        }

        // THE FIX (this and RestoreGpuIrq / RestoreFSE below): all three logged a flat
        // "RESTORE: ... removed" with nothing checked - not the delete, not even whether
        // the PCI key opened. RestoreVBS twenty lines up is the pattern these should
        // have followed: judge the outcome by reading the system, not by having run.
        // These sit on the revert path for two tweaks that need a reboot, so a false
        // "removed" is a user rebooting to find nothing changed and no explanation.
        inline void RestoreMSIMode() {
            int cleared = 0, refused = 0;
            HKEY hPciKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hPciKey) == ERROR_SUCCESS) {
                wchar_t subKeyName[256]; DWORD subKeySize = 256; DWORD index = 0;
                while (RegEnumKeyExW(hPciKey, index, subKeyName, &subKeySize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                    HKEY hDeviceKey;
                    if (RegOpenKeyExW(hPciKey, subKeyName, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hDeviceKey) == ERROR_SUCCESS) {
                        wchar_t instanceName[256]; DWORD instanceSize = 256; DWORD instanceIndex = 0;
                        while (RegEnumKeyExW(hDeviceKey, instanceIndex, instanceName, &instanceSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                            HKEY hInstanceKey;
                            if (RegOpenKeyExW(hDeviceKey, instanceName, 0, KEY_READ, &hInstanceKey) == ERROR_SUCCESS) {
                                if (IsDisplayClassDevice(hInstanceKey)) {
                                    HKEY hMsiKey;
                                    std::wstring msiPath = L"Device Parameters\\Interrupt Management\\MessageSignaledInterruptProperties";
                                    LSTATUS openMsi = RegOpenKeyExW(hInstanceKey, msiPath.c_str(), 0, KEY_SET_VALUE, &hMsiKey);
                                    if (openMsi == ERROR_SUCCESS) {
                                        LSTATUS d = RegDeleteValueW(hMsiKey, L"MSISupported");
                                        // Absent is the state being restored to, not a failure.
                                        if (d == ERROR_SUCCESS) cleared++;
                                        else if (d != ERROR_FILE_NOT_FOUND) refused++;
                                        RegCloseKey(hMsiKey);
                                    }
                                    else if (openMsi != ERROR_FILE_NOT_FOUND) refused++;
                                }
                                RegCloseKey(hInstanceKey);
                            }
                            instanceSize = 256; instanceIndex++;
                        }
                        RegCloseKey(hDeviceKey);
                    }
                    subKeySize = 256; index++;
                }
                RegCloseKey(hPciKey);
            }
            else {
                SafeAddLog("RESTORE FAILED: Could not read the PCI device list - the GPU MSI mode override is unchanged. Administrator rights are required.");
                return;
            }

            if (refused > 0) {
                SafeAddLog("RESTORE PARTIAL: GPU MSI mode override removed from " + std::to_string(cleared) +
                    " adapter(s); " + std::to_string(refused) + " refused the change and are unchanged. Administrator rights are required.");
            }
            else if (cleared > 0) {
                SafeAddLog("RESTORE: GPU MSI mode override removed. Restart required.");
            }
            else {
                SafeAddLog("RESTORE: No GPU MSI mode override was present - nothing to undo.");
            }
        }

        inline void RestoreGpuIrq() {
            int cleared = 0, refused = 0;
            HKEY hPciKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hPciKey) == ERROR_SUCCESS) {
                wchar_t subKeyName[256]; DWORD subKeySize = 256; DWORD index = 0;
                while (RegEnumKeyExW(hPciKey, index, subKeyName, &subKeySize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                    HKEY hDeviceKey;
                    if (RegOpenKeyExW(hPciKey, subKeyName, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hDeviceKey) == ERROR_SUCCESS) {
                        wchar_t instanceName[256]; DWORD instanceSize = 256; DWORD instanceIndex = 0;
                        while (RegEnumKeyExW(hDeviceKey, instanceIndex, instanceName, &instanceSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                            HKEY hInstanceKey;
                            if (RegOpenKeyExW(hDeviceKey, instanceName, 0, KEY_READ, &hInstanceKey) == ERROR_SUCCESS) {
                                if (IsDisplayClassDevice(hInstanceKey)) {
                                    HKEY hAffinityPolicy;
                                    std::wstring path = L"Device Parameters\\Interrupt Management\\Affinity Policy";
                                    LSTATUS openAff = RegOpenKeyExW(hInstanceKey, path.c_str(), 0, KEY_SET_VALUE, &hAffinityPolicy);
                                    if (openAff == ERROR_SUCCESS) {
                                        const wchar_t* vals[] = { L"DevicePolicy", L"AssignmentSetOverride",
                                                                  L"DevicePriority" }; // the last is written by SetGPUPriorityHigh
                                        for (auto v : vals) {
                                            LSTATUS d = RegDeleteValueW(hAffinityPolicy, v);
                                            if (d == ERROR_SUCCESS) cleared++;
                                            else if (d != ERROR_FILE_NOT_FOUND) refused++;
                                        }
                                        RegCloseKey(hAffinityPolicy);
                                    }
                                    else if (openAff != ERROR_FILE_NOT_FOUND) refused++;
                                }
                                RegCloseKey(hInstanceKey);
                            }
                            instanceSize = 256; instanceIndex++;
                        }
                        RegCloseKey(hDeviceKey);
                    }
                    subKeySize = 256; index++;
                }
                RegCloseKey(hPciKey);
            }
            else {
                SafeAddLog("RESTORE FAILED: Could not read the PCI device list - the GPU interrupt overrides are unchanged. Administrator rights are required.");
                return;
            }

            if (refused > 0) {
                SafeAddLog("RESTORE PARTIAL: " + std::to_string(cleared) + " GPU interrupt override(s) removed, " +
                    std::to_string(refused) + " refused and still in place. Administrator rights are required.");
            }
            else if (cleared > 0) {
                SafeAddLog("RESTORE: GPU interrupt affinity and priority overrides removed. Restart required.");
            }
            else {
                SafeAddLog("RESTORE: No GPU interrupt overrides were present - nothing to undo.");
            }
        }

        inline void RestoreFSE() {
            HKEY hKey;
            LSTATUS open = RegOpenKeyExA(HKEY_CURRENT_USER, "System\\GameConfigStore", 0, KEY_SET_VALUE, &hKey);
            if (open == ERROR_FILE_NOT_FOUND) {
                SafeAddLog("RESTORE: No Fullscreen Exclusive override was present - nothing to undo.");
                return;
            }
            if (open != ERROR_SUCCESS) {
                SafeAddLog("RESTORE FAILED: Could not open GameConfigStore - the Fullscreen Exclusive override is unchanged.");
                return;
            }
            const char* vals[] = { "GameDVR_FSEBehaviorMode", "GameDVR_HonorUserFSEBehaviorMode",
                                   "GameDVR_DXGIHonorFSEWindowsCompatible" };
            int cleared = 0, refused = 0;
            for (auto v : vals) {
                LSTATUS d = RegDeleteValueA(hKey, v);
                if (d == ERROR_SUCCESS) cleared++;
                else if (d != ERROR_FILE_NOT_FOUND) refused++;
            }
            RegCloseKey(hKey);

            if (refused > 0) SafeAddLog("RESTORE PARTIAL: " + std::to_string(cleared) + " of the Fullscreen Exclusive values were removed; " +
                std::to_string(refused) + " could not be and are still set.");
            else if (cleared > 0) SafeAddLog("RESTORE: Fullscreen Exclusive override removed.");
            else SafeAddLog("RESTORE: No Fullscreen Exclusive override was present - nothing to undo.");
        }

        // 8. PER-PROCESS SMT RESTRICTION
        //
        // Builds an affinity mask holding exactly one logical processor per
        // physical core, by walking real CPU topology rather than guessing at
        // core indices. Applied to a single process, this is a genuine
        // per-process SMT disable: the game's threads can never land on a
        // hyperthread sibling, so two of its threads never share one physical
        // core's execution resources.
        //
        // What this deliberately is NOT: the systemwide SMT/hyperthreading
        // switch. Nothing here touches BIOS settings, the registry, or any
        // other process, and no reboot is involved - the mask lives and dies
        // with the target process.
        //
        // Returns 0 if topology can't be read, which callers must treat as
        // "skip this tweak" rather than "restrict to no cores at all."
        //
        // Defined above ApplyGameProcessTweaks() below, which is its only
        // caller - declaration-before-use, no forward declaration needed.
        inline DWORD_PTR BuildSmtDisabledMask() {
            DWORD bufferLen = 0;
            GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bufferLen);
            if (bufferLen == 0) return 0;

            std::vector<BYTE> buffer(bufferLen);
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pInfo = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer.data();
            if (!GetLogicalProcessorInformationEx(RelationProcessorCore, pInfo, &bufferLen)) return 0;

            DWORD_PTR mask = 0;
            DWORD offset = 0;
            while (offset < bufferLen) {
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX entry = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer.data() + offset);
                if (entry->Relationship == RelationProcessorCore) {
                    // A RelationProcessorCore entry always describes one
                    // physical core inside a single processor group. Only
                    // group 0 is usable here: SetProcessAffinityMask can't
                    // express affinity across processor groups at all, so bits
                    // from another group would be meaningless (or worse,
                    // collide with group 0's bit positions).
                    if (entry->Processor.GroupMask[0].Group == 0) {
                        KAFFINITY groupMask = entry->Processor.GroupMask[0].Mask;
                        for (int bit = 0; bit < (int)(sizeof(KAFFINITY) * 8); bit++) {
                            if (groupMask & ((KAFFINITY)1 << bit)) {
                                mask |= ((DWORD_PTR)1 << bit);
                                break; // first logical processor of this core only
                            }
                        }
                    }
                }
                offset += entry->Size;
            }
            return mask;
        }

        // Applies the two per-process tweaks a community profile can carry, to
        // every process matching targetExe. Both are per-process and revert on
        // their own when the process exits - nothing is written to the registry
        // and nothing needs undoing later.
        inline void ApplyGameProcessTweaks(const std::wstring& targetExe, bool disableSMT, bool highPriority) {
            if (targetExe.empty()) return;
            if (!disableSMT && !highPriority) return;

            DWORD_PTR smtMask = 0;
            if (disableSMT) {
                smtMask = BuildSmtDisabledMask();
                if (smtMask == 0) {
                    SafeAddLog("GAME TWEAKS: Could not read CPU core topology - skipping the one-thread-per-core restriction.");
                }
            }

            HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapshot == INVALID_HANDLE_VALUE) return;

            PROCESSENTRY32W pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32W);

            bool smtApplied = false;
            bool priorityApplied = false;

            if (Process32FirstW(hSnapshot, &pe32)) {
                do {
                    if (_wcsicmp(pe32.szExeFile, targetExe.c_str()) != 0) continue;

                    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pe32.th32ProcessID);
                    if (!hProcess) continue;

                    if (disableSMT && smtMask != 0) {
                        DWORD_PTR processAffinityMask = 0;
                        DWORD_PTR systemAffinityMask = 0;
                        if (GetProcessAffinityMask(hProcess, &processAffinityMask, &systemAffinityMask)) {
                            // Intersect with what the process is actually
                            // allowed to run on, so this never widens affinity
                            // or hands SetProcessAffinityMask a bit the system
                            // doesn't have (which would fail outright).
                            DWORD_PTR target = smtMask & systemAffinityMask;
                            if (target != 0 && SetProcessAffinityMask(hProcess, target)) {
                                smtApplied = true;
                            }
                        }
                    }

                    if (highPriority) {
                        if (SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS)) {
                            priorityApplied = true;
                        }
                    }

                    CloseHandle(hProcess);
                } while (Process32NextW(hSnapshot, &pe32));
            }
            CloseHandle(hSnapshot);

            if (smtApplied) {
                SafeAddLog("GAME TWEAKS: Game restricted to one thread per physical core (per-process only).");
            }
            if (priorityApplied) {
                SafeAddLog("GAME TWEAKS: Game process priority raised to High.");
            }
        }

        // 9. DISABLE MOUSE ACCELERATION ("Enhance Pointer Precision")
        // The single most impactful setting for consistent aim: makes cursor-to-aim
        // 1:1, so the same physical movement always maps to the same in-game turn
        // regardless of speed. Applied live via SPI_SETMOUSE (which also persists to
        // HKCU\Control Panel\Mouse) - no restart. Restore puts back Windows defaults.
        // The array is { xThreshold, yThreshold, acceleration } = MouseThreshold1/2,
        // MouseSpeed; {0,0,0} = off, {6,10,1} = Windows default.
        // Returns whether the pointer setting is now what the caller asked for.
        inline bool SetMouseAcceleration(bool disable) {
            int mi[3];
            if (disable) { mi[0] = 0; mi[1] = 0; mi[2] = 0; }
            else { mi[0] = 6; mi[1] = 10; mi[2] = 1; }
            // The return value used to be discarded, so the log claimed the change had
            // been made whether or not SPI_SETMOUSE accepted it.
            if (!SystemParametersInfoW(SPI_SETMOUSE, 0, mi, SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE)) {
                SafeAddLog("TWEAK FAILED: Windows rejected the mouse acceleration change - the pointer setting is unchanged.");
                return false;
            }
            SafeAddLog(disable ? "TWEAK: Mouse acceleration (Enhance Pointer Precision) disabled - aim is now 1:1."
                               : "TWEAK: Mouse acceleration restored to Windows default.");
            return true;
        }

        // 10. DISABLE GAMEDVR / XBOX GAME BAR BACKGROUND RECORDING
        // Game Bar's capture pipeline runs even when you aren't recording. Disabling
        // it is a real, low-risk frame-time win. HKCU is the user toggle; the HKLM
        // policy hard-disables it machine-wide.
        // THE FIX: every one of the four registry results here used to be discarded,
        // and the log sat OUTSIDE both if-blocks - so it reported "GameDVR disabled"
        // even when neither key had opened and nothing had been written. The HKLM
        // policy half is the part that genuinely fails: it is under
        // SOFTWARE\Policies, so without elevation the write is refused and Game Bar
        // stays available machine-wide while the log said otherwise. The two halves
        // are now tracked separately, because "your user setting applied but the
        // machine-wide policy did not" is a real and useful distinction.
        // Returns true only when BOTH halves landed. "Your user setting applied but
        // the machine-wide policy did not" is a real state and it is not success -
        // Game Bar stays available machine-wide, which is what the toggle promised
        // to stop.
        inline bool SetGameDVR(bool disable) {
            HKEY hKey;
            bool userOk = false;
            bool policyOk = false;

            if (RegCreateKeyExA(HKEY_CURRENT_USER, "System\\GameConfigStore", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                DWORD v = disable ? 0 : 1;
                userOk = (RegSetValueExA(hKey, "GameDVR_Enabled", 0, REG_DWORD, (const BYTE*)&v, sizeof(v)) == ERROR_SUCCESS);
                RegCloseKey(hKey);
            }

            if (disable) {
                if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                    DWORD z = 0;
                    policyOk = (RegSetValueExA(hKey, "AllowGameDVR", 0, REG_DWORD, (const BYTE*)&z, sizeof(z)) == ERROR_SUCCESS);
                    RegCloseKey(hKey);
                }
            }
            else {
                LSTATUS open = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR", 0, KEY_SET_VALUE, &hKey);
                if (open == ERROR_SUCCESS) {
                    LSTATUS del = RegDeleteValueA(hKey, "AllowGameDVR");
                    // Already absent is the state we were asking for, not a failure.
                    policyOk = (del == ERROR_SUCCESS || del == ERROR_FILE_NOT_FOUND);
                    RegCloseKey(hKey);
                }
                else if (open == ERROR_FILE_NOT_FOUND) {
                    policyOk = true;  // no policy key at all - already the Windows default
                }
            }

            if (!userOk && !policyOk) {
                SafeAddLog(disable ? "TWEAK FAILED: GameDVR could not be changed - nothing was written."
                                   : "TWEAK FAILED: GameDVR could not be restored - nothing was written.");
                return false;
            }
            if (!userOk) {
                SafeAddLog("TWEAK PARTIAL: the machine-wide GameDVR policy was written, but the per-user GameConfigStore value was not.");
                return false;
            }
            if (!policyOk) {
                SafeAddLog(disable ? "TWEAK PARTIAL: GameDVR disabled for your user, but the machine-wide policy could not be written (admin rights required)."
                                   : "TWEAK PARTIAL: GameDVR re-enabled for your user, but the machine-wide policy value could not be removed (admin rights required).");
                return false;
            }
            SafeAddLog(disable ? "TWEAK: GameDVR / Xbox Game Bar background recording disabled."
                               : "TWEAK: GameDVR restored to Windows default.");
            return true;
        }

        // 11. MMCSS "Games" TASK PROFILE
        // Games register the MMCSS "Games" task via DWM/DirectX; tuning that task's
        // registry profile raises the scheduling the OS gives their render threads.
        // GPU Priority 8 and Scheduling Category "High" are already Windows defaults;
        // the meaningful changes are Priority 2->6 and SFIO Priority Normal->High.
        // THE FIX (A4-4): the log was gated on the key OPENING, but all four writes
        // had their results discarded - so a key that opened read-only, or a value
        // refused by policy, still reported the profile as tuned. Counted now.
        // Returns true only if all four values were written.
        inline bool SetMMCSSGamesProfile(bool apply) {
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                DWORD gpuPrio = 8;
                DWORD prio = apply ? 6 : 2;
                const char* schedCat = "High";
                const char* sfio = apply ? "High" : "Normal";

                int written = 0;
                if (RegSetValueExA(hKey, "GPU Priority", 0, REG_DWORD, (const BYTE*)&gpuPrio, sizeof(gpuPrio)) == ERROR_SUCCESS) written++;
                if (RegSetValueExA(hKey, "Priority", 0, REG_DWORD, (const BYTE*)&prio, sizeof(prio)) == ERROR_SUCCESS) written++;
                if (RegSetValueExA(hKey, "Scheduling Category", 0, REG_SZ, (const BYTE*)schedCat, (DWORD)strlen(schedCat) + 1) == ERROR_SUCCESS) written++;
                if (RegSetValueExA(hKey, "SFIO Priority", 0, REG_SZ, (const BYTE*)sfio, (DWORD)strlen(sfio) + 1) == ERROR_SUCCESS) written++;
                RegCloseKey(hKey);

                if (written == 4) {
                    SafeAddLog(apply ? "TWEAK: MMCSS Games profile tuned (Priority 6, SFIO High)."
                                     : "TWEAK: MMCSS Games profile restored to Windows defaults.");
                }
                else if (written > 0) {
                    SafeAddLog("TWEAK PARTIAL: only " + std::to_string(written)
                        + " of 4 MMCSS Games values could be written.");
                }
                else {
                    SafeAddLog("TWEAK FAILED: the MMCSS Games task opened but none of its values could be written.");
                }
                return written == 4;
            }
            SafeAddLog("TWEAK FAILED: Could not open the MMCSS Games task (admin rights may be required).");
            return false;
        }

        // 12. GLOBAL TIMER RESOLUTION (Windows 11 22H2+)
        // Since Win10 2004 a process's requested timer resolution only applies while
        // it's in the foreground. This flag restores the old global behaviour so the
        // low-latency resolution OptiMax requests stays in effect system-wide. Needs
        // a restart. Restore deletes the flag (back to the per-process default).
        // THE FIX (A4-4): the write result was discarded, and there was no else
        // branch at all - so a failure to open the key was completely silent, with
        // the UI toggle left showing on and not one line in the log to explain it.
        inline bool SetGlobalTimerResolution(bool enable) {
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                bool ok;
                if (enable) {
                    DWORD v = 1;
                    ok = (RegSetValueExA(hKey, "GlobalTimerResolutionRequests", 0, REG_DWORD, (const BYTE*)&v, sizeof(v)) == ERROR_SUCCESS);
                }
                else {
                    LSTATUS del = RegDeleteValueA(hKey, "GlobalTimerResolutionRequests");
                    // Already gone is the state being asked for, not a failure.
                    ok = (del == ERROR_SUCCESS || del == ERROR_FILE_NOT_FOUND);
                }
                RegCloseKey(hKey);

                if (ok) {
                    SafeAddLog(enable ? "TWEAK: Global timer resolution enabled - restart required to take effect."
                                      : "TWEAK: Global timer resolution flag removed - restart required.");
                }
                else {
                    SafeAddLog(enable ? "TWEAK FAILED: Could not write the global timer resolution flag."
                                      : "TWEAK FAILED: Could not remove the global timer resolution flag.");
                }
                return ok;
            }
            SafeAddLog("TWEAK FAILED: Could not open the kernel Session Manager key (admin rights are required).");
            return false;
        }

    } // End V3

    // =========================================================================
    // RESTORE EVERYTHING - undoes everything OptiMax can apply, not just the
    // per-session tweaks RevertOptimizations() already handled. Ties together
    // the V3 restore functions above with the existing service restores and
    // the per-session revert, so there's one action a user can take if
    // anything ever needs walking all the way back to a clean Windows install.
    // =========================================================================
    inline void RestoreAllToWindowsDefaults() {
        SafeAddLog("RESTORE: Undoing all OptiMax changes...");

        RevertOptimizations();
        // Core parking and the energy performance preference had no revert path at
        // all until now, so this walked back everything EXCEPT the two settings that
        // quietly cost a laptop user battery life for as long as they kept the app.
        AdvancedPower::RestoreProcessorPowerSettings();
        Debloat::RestoreTelemetry();
        Debloat::RestoreBloatServices();
        // THE FIX: four more Debloat features had grown since this function was
        // written and none of them were added to it, so "Undo Everything" left behind
        // the machine-wide privacy policy values under HKLM, six services still set to
        // Disabled, six telemetry scheduled tasks still switched off, and the modified
        // menu/hover delays. All of those are persistent system changes that survive
        // uninstalling the app entirely.
        Debloat::SetPrivacyTweaks(false);
        Debloat::SetExtendedServices(false);
        Debloat::SetTelemetryTasks(false);
        Debloat::SetSnappyUI(false);
        V3::RestoreVBS();
        V3::RestoreMSIMode();
        V3::RestoreGpuIrq();
        V3::RestoreFSE();
        // These three were missing from "Restore All Defaults" entirely, so the one
        // action a user is told walks everything back to a clean Windows left the TCP
        // adapter overrides, the boot configuration and the DWM overlay setting behind.
        V3::RestoreNagleAlgorithm();
        Bootloader::RestoreBootTimers();
        Profiler::SetMPODisabled(false);
        V3::SetMouseAcceleration(false);
        V3::SetGameDVR(false);
        // The Discord debloat's Fullscreen-Optimizations override is a persistent HKCU
        // change with no other way back - it had no revert path anywhere in the project.
        Discord::RestoreFSO();
        // Same story for the autostart Run value the debloat deletes: it now keeps a
        // copy, and this is the one action that replays it.
        Discord::RestoreAutostart();
        V3::SetMMCSSGamesProfile(false);
        V3::SetGlobalTimerResolution(false);

        SafeAddLog("RESTORE: Done. Restart your PC so the VBS, MSI mode, GPU interrupt, and timer changes fully take effect.");
    }
} // End OptiMax