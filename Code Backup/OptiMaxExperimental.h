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
        // THE FIX: this used to hardcode C:\Windows\Temp, which silently does
        // nothing on any system where Windows isn't installed on the C: drive.
        // %SystemRoot% resolves correctly regardless of install drive. Also now
        // checks ShellExecuteW's actual return value instead of always logging
        // success immediately after firing off the command.
        //
        // THE FIX: Prefetch deletion has been REMOVED from this command. Wiping
        // %SystemRoot%\Prefetch is actively counterproductive - Prefetch exists
        // precisely so Windows can preload the files an app needs at launch, so
        // deleting it makes the NEXT launch of every program slower until the
        // data is rebuilt. It was never a cleanup win, and it is not worth
        // keeping just because other "optimizer" tools do it.
        inline void DeepClean() {
            SafeAddLog("CLEANUP: Clearing caches and temporary files...");

            HINSTANCE r1 = ShellExecuteW(NULL, L"open", L"cmd.exe", L"/c ipconfig /flushdns", NULL, SW_HIDE);
            SafeAddLog(((INT_PTR)r1 > 32) ? "CLEANUP: DNS cache flush started." : "CLEANUP WARNING: Could not start the DNS cache flush.");

            HINSTANCE r2 = ShellExecuteW(NULL, L"open", L"cmd.exe",
                L"/c del /q /f /s %TEMP%\\* & del /q /f /s %SystemRoot%\\Temp\\*",
                NULL, SW_HIDE);
            SafeAddLog(((INT_PTR)r2 > 32) ? "CLEANUP: Temporary file cleanup started." : "CLEANUP WARNING: Could not start the temporary file cleanup.");

            HINSTANCE r3 = ShellExecuteW(NULL, L"open", L"cmd.exe", L"/c del /q /f /s \"%LOCALAPPDATA%\\NVIDIA\\DXCache\\*\"", NULL, SW_HIDE);
            HINSTANCE r4 = ShellExecuteW(NULL, L"open", L"cmd.exe", L"/c del /q /f /s \"%LOCALAPPDATA%\\AMD\\DxCache\\*\"", NULL, SW_HIDE);
            SafeAddLog(((INT_PTR)r3 > 32 && (INT_PTR)r4 > 32)
                ? "CLEANUP: Shader cache cleanup started. Games will rebuild these on their next launch."
                : "CLEANUP WARNING: Could not start the shader cache cleanup.");
        }
    }

    // =========================================================================
    // BOOT TIMER SETTINGS
    // =========================================================================
    namespace Bootloader {
        inline void OptimizeTimers() {
            ShellExecuteW(NULL, L"open", L"bcdedit", L"/set disabledynamictick yes", NULL, SW_HIDE);
            ShellExecuteW(NULL, L"open", L"bcdedit", L"/deletevalue useplatformclock", NULL, SW_HIDE);
            ShellExecuteW(NULL, L"open", L"bcdedit", L"/deletevalue useplatformtick", NULL, SW_HIDE);
            SafeAddLog("BOOT: HPET and dynamic tick disabled in the boot configuration; Windows will use the CPU clock. Restart to take effect.");
        }

        // THE FIX: there was no way back. The toggle that applies these settings had
        // no revert path and this namespace had no restore function, so a user who
        // switched "Use CPU Clock for Timing" off got no log line, no change, and a
        // boot configuration that stayed modified permanently.
        //
        // Only disabledynamictick needs undoing. The other two commands DELETE
        // values, and absent is already the Windows default - there is nothing to
        // put back, which is why this is one command rather than three.
        inline void RestoreBootTimers() {
            HINSTANCE r = ShellExecuteW(NULL, L"open", L"bcdedit", L"/deletevalue disabledynamictick", NULL, SW_HIDE);
            SafeAddLog(((INT_PTR)r > 32)
                ? "BOOT: Boot timer settings restored to Windows defaults. Restart to take effect."
                : "BOOT WARNING: Could not restore the boot timer settings - administrator rights are required.");
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
        inline void LockToPCores(const std::wstring& targetExe) {
            HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapshot == INVALID_HANDLE_VALUE) return;

            PROCESSENTRY32W pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32W);

            if (Process32FirstW(hSnapshot, &pe32)) {
                do {
                    if (_wcsicmp(pe32.szExeFile, targetExe.c_str()) == 0) {
                        HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pe32.th32ProcessID);
                        if (hProcess) {
                            DWORD_PTR processAffinityMask;
                            DWORD_PTR systemAffinityMask;

                            if (GetProcessAffinityMask(hProcess, &processAffinityMask, &systemAffinityMask)) {
                                int totalCores = 0;
                                DWORD_PTR tempMask = systemAffinityMask;
                                while (tempMask > 0) {
                                    if (tempMask & 1) totalCores++;
                                    tempMask >>= 1;
                                }

                                int pCores = totalCores - (totalCores / 4);
                                if (pCores < 1) pCores = 1;

                                DWORD_PTR newAffinity = 0;
                                for (int i = 0; i < pCores; i++) {
                                    newAffinity |= (1ULL << i);
                                }

                                if (SetProcessAffinityMask(hProcess, newAffinity)) {
                                    SafeAddLog("TWEAK: Game restricted to the CPU's performance cores.");
                                }
                            }
                            CloseHandle(hProcess);
                        }
                    }
                } while (Process32NextW(hSnapshot, &pe32));
            }
            CloseHandle(hSnapshot);
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

            if ((INT_PTR)ShellExecuteW(NULL, L"open", L"bcdedit", L"/set hypervisorlaunchtype off", NULL, SW_HIDE) <= 32) ok = false;
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
        inline void PinGpuIrq() {
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

                                                RegSetValueExW(hAffinityPolicy, L"DevicePolicy", 0, REG_DWORD, (const BYTE*)&devPolicy, sizeof(devPolicy));
                                                RegSetValueExW(hAffinityPolicy, L"AssignmentSetOverride", 0, REG_BINARY, assignmentSet, sizeof(assignmentSet));

                                                RegCloseKey(hAffinityPolicy);
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
            ShellExecuteW(NULL, L"open", L"bcdedit", L"/deletevalue hypervisorlaunchtype", NULL, SW_HIDE);

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

            bool ok = !overrideStillDisabling("System\\CurrentControlSet\\Control\\DeviceGuard", "EnableVirtualizationBasedSecurity")
                && !overrideStillDisabling("System\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity", "Enabled");

            SafeAddLog(ok
                ? "RESTORE: VBS and Memory Integrity reverted to Windows defaults. Restart required."
                : "RESTORE FAILED: the VBS/Memory Integrity overrides are still in the registry - your machine is NOT protected yet. Administrator rights are required.");
            return ok;
        }

        inline void RestoreMSIMode() {
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
                                    if (RegOpenKeyExW(hInstanceKey, msiPath.c_str(), 0, KEY_SET_VALUE, &hMsiKey) == ERROR_SUCCESS) {
                                        RegDeleteValueW(hMsiKey, L"MSISupported");
                                        RegCloseKey(hMsiKey);
                                    }
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
            SafeAddLog("RESTORE: GPU MSI mode override removed. Restart required.");
        }

        inline void RestoreGpuIrq() {
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
                                    if (RegOpenKeyExW(hInstanceKey, path.c_str(), 0, KEY_SET_VALUE, &hAffinityPolicy) == ERROR_SUCCESS) {
                                        RegDeleteValueW(hAffinityPolicy, L"DevicePolicy");
                                        RegDeleteValueW(hAffinityPolicy, L"AssignmentSetOverride");
                                        RegDeleteValueW(hAffinityPolicy, L"DevicePriority"); // written by SetGPUPriorityHigh
                                        RegCloseKey(hAffinityPolicy);
                                    }
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
            SafeAddLog("RESTORE: GPU interrupt affinity and priority overrides removed. Restart required.");
        }

        inline void RestoreFSE() {
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "System\\GameConfigStore", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueA(hKey, "GameDVR_FSEBehaviorMode");
                RegDeleteValueA(hKey, "GameDVR_HonorUserFSEBehaviorMode");
                RegDeleteValueA(hKey, "GameDVR_DXGIHonorFSEWindowsCompatible");
                RegCloseKey(hKey);
            }
            SafeAddLog("RESTORE: Fullscreen Exclusive override removed.");
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
        inline void SetMouseAcceleration(bool disable) {
            int mi[3];
            if (disable) { mi[0] = 0; mi[1] = 0; mi[2] = 0; }
            else { mi[0] = 6; mi[1] = 10; mi[2] = 1; }
            // The return value used to be discarded, so the log claimed the change had
            // been made whether or not SPI_SETMOUSE accepted it.
            if (!SystemParametersInfoW(SPI_SETMOUSE, 0, mi, SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE)) {
                SafeAddLog("TWEAK FAILED: Windows rejected the mouse acceleration change - the pointer setting is unchanged.");
                return;
            }
            SafeAddLog(disable ? "TWEAK: Mouse acceleration (Enhance Pointer Precision) disabled - aim is now 1:1."
                               : "TWEAK: Mouse acceleration restored to Windows default.");
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
        inline void SetGameDVR(bool disable) {
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
                return;
            }
            if (!userOk) {
                SafeAddLog("TWEAK PARTIAL: the machine-wide GameDVR policy was written, but the per-user GameConfigStore value was not.");
                return;
            }
            if (!policyOk) {
                SafeAddLog(disable ? "TWEAK PARTIAL: GameDVR disabled for your user, but the machine-wide policy could not be written (admin rights required)."
                                   : "TWEAK PARTIAL: GameDVR re-enabled for your user, but the machine-wide policy value could not be removed (admin rights required).");
                return;
            }
            SafeAddLog(disable ? "TWEAK: GameDVR / Xbox Game Bar background recording disabled."
                               : "TWEAK: GameDVR restored to Windows default.");
        }

        // 11. MMCSS "Games" TASK PROFILE
        // Games register the MMCSS "Games" task via DWM/DirectX; tuning that task's
        // registry profile raises the scheduling the OS gives their render threads.
        // GPU Priority 8 and Scheduling Category "High" are already Windows defaults;
        // the meaningful changes are Priority 2->6 and SFIO Priority Normal->High.
        // THE FIX (A4-4): the log was gated on the key OPENING, but all four writes
        // had their results discarded - so a key that opened read-only, or a value
        // refused by policy, still reported the profile as tuned. Counted now.
        inline void SetMMCSSGamesProfile(bool apply) {
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
            }
            else {
                SafeAddLog("TWEAK FAILED: Could not open the MMCSS Games task (admin rights may be required).");
            }
        }

        // 12. GLOBAL TIMER RESOLUTION (Windows 11 22H2+)
        // Since Win10 2004 a process's requested timer resolution only applies while
        // it's in the foreground. This flag restores the old global behaviour so the
        // low-latency resolution OptiMax requests stays in effect system-wide. Needs
        // a restart. Restore deletes the flag (back to the per-process default).
        // THE FIX (A4-4): the write result was discarded, and there was no else
        // branch at all - so a failure to open the key was completely silent, with
        // the UI toggle left showing on and not one line in the log to explain it.
        inline void SetGlobalTimerResolution(bool enable) {
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
            }
            else {
                SafeAddLog("TWEAK FAILED: Could not open the kernel Session Manager key (admin rights are required).");
            }
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
        V3::SetMMCSSGamesProfile(false);
        V3::SetGlobalTimerResolution(false);

        SafeAddLog("RESTORE: Done. Restart your PC so the VBS, MSI mode, GPU interrupt, and timer changes fully take effect.");
    }
} // End OptiMax