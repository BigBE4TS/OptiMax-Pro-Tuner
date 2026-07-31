#pragma once
#include <windows.h>
#include <string>

// Shcore is needed for the per-monitor DPI call in OptiMax_Profiler.cpp (the comment
// here used to say "main.cpp", which this project has never had). The pragma lives in
// this header only because it is the one every translation unit already pulls in - it
// has nothing to do with verification, and belongs with the DPI code if that ever moves.
#pragma comment(lib, "Shcore.lib")

// (SystemRegistryState lived here and was removed - a two-field struct holding
// isMSIModeVerified / isNetworkTweaked with zero references anywhere in the tree,
// including the UI wiring. Both checks below are called directly and their results
// used inline, so nothing ever needed somewhere to put them.)

class OptiMaxVerifier {
public:
    // THE FIX: CheckMSIMode() below identified the GPU by the legacy "Class" value
    // and compared it to L"Display". VERIFIED ON A REAL MACHINE: not one of its 41
    // PCI device instances carries a "Class" value - only "ClassGUID" - so this
    // could never match anything and the verifier could never confirm a tweak no
    // matter what had been applied. ClassGUID is what Windows always writes.
    //
    // Deliberately duplicated from OptiMaxExperimental.h's IsDisplayClassDevice()
    // rather than shared: this header is included on its own and does not pull in
    // the experimental module, and include order in this project is already
    // load-bearing enough without adding another edge to it.
    static bool IsDisplayDevice(HKEY hInstanceKey) {
        auto readString = [](HKEY k, const wchar_t* name, wchar_t* buf, DWORD bufBytes) -> bool {
            buf[0] = L'\0';
            DWORD size = bufBytes - sizeof(wchar_t); // room to force a terminator
            DWORD type = 0;
            if (RegQueryValueExW(k, name, NULL, &type, (LPBYTE)buf, &size) != ERROR_SUCCESS) return false;
            if (type != REG_SZ && type != REG_EXPAND_SZ) return false;
            buf[size / sizeof(wchar_t)] = L'\0';   // the registry does not guarantee one
            return true;
            };

        wchar_t buf[256];
        if (readString(hInstanceKey, L"ClassGUID", buf, sizeof(buf))
            && _wcsicmp(buf, L"{4d36e968-e325-11ce-bfc1-08002be10318}") == 0) return true;
        if (readString(hInstanceKey, L"Class", buf, sizeof(buf))
            && _wcsicmp(buf, L"Display") == 0) return true;
        return false;
    }

    // THE FIX: this used to check "SYSTEM\CurrentControlSet\Control\GraphicsDrivers\MSISupported",
    // which has nothing to do with where InjectMSIMode() actually writes. MSI mode is
    // configured per-device under SYSTEM\CurrentControlSet\Enum\PCI\<device>\<instance>\
    // Device Parameters\Interrupt Management\MessageSignaledInterruptProperties\MSISupported.
    // The old check was reading a path that essentially never contains this value, so it
    // would report "not verified" regardless of whether MSI mode had actually been applied.
    static bool CheckMSIMode() {
        HKEY hPciKey;
        bool verified = false;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hPciKey) == ERROR_SUCCESS) {
            wchar_t subKeyName[256];
            DWORD subKeySize = 256;
            DWORD index = 0;

            while (!verified && RegEnumKeyExW(hPciKey, index, subKeyName, &subKeySize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                HKEY hDeviceKey;
                if (RegOpenKeyExW(hPciKey, subKeyName, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hDeviceKey) == ERROR_SUCCESS) {
                    wchar_t instanceName[256];
                    DWORD instanceSize = 256;
                    DWORD instanceIndex = 0;

                    while (!verified && RegEnumKeyExW(hDeviceKey, instanceIndex, instanceName, &instanceSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                        HKEY hInstanceKey;
                        if (RegOpenKeyExW(hDeviceKey, instanceName, 0, KEY_READ, &hInstanceKey) == ERROR_SUCCESS) {
                            if (IsDisplayDevice(hInstanceKey)) {
                                HKEY hMsiKey;
                                std::wstring msiPath = L"Device Parameters\\Interrupt Management\\MessageSignaledInterruptProperties";
                                if (RegOpenKeyExW(hInstanceKey, msiPath.c_str(), 0, KEY_READ, &hMsiKey) == ERROR_SUCCESS) {
                                    DWORD msiSupported = 0;
                                    DWORD dataSize = sizeof(msiSupported);
                                    if (RegQueryValueExW(hMsiKey, L"MSISupported", NULL, NULL, (LPBYTE)&msiSupported, &dataSize) == ERROR_SUCCESS) {
                                        if (msiSupported == 1) verified = true;
                                    }
                                    RegCloseKey(hMsiKey);
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
        return verified;
    }

    static bool CheckNetworkTweaks() {
        HKEY hKey;
        bool verified = false;
        const char* subKey = "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD index = 0;
            char interfaceName[256];
            DWORD nameSize = sizeof(interfaceName);
            while (RegEnumKeyExA(hKey, index, interfaceName, &nameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                HKEY hIntKey;
                std::string path = std::string(subKey) + "\\" + interfaceName;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &hIntKey) == ERROR_SUCCESS) {
                    DWORD tcpNoDelay = 0;
                    DWORD size = sizeof(tcpNoDelay);
                    if (RegQueryValueExA(hIntKey, "TCPNoDelay", NULL, NULL, (LPBYTE)&tcpNoDelay, &size) == ERROR_SUCCESS) {
                        if (tcpNoDelay == 1) verified = true;
                    }
                    RegCloseKey(hIntKey);
                }
                if (verified) break; // Found at least one interface tweaked
                index++;
                nameSize = sizeof(interfaceName);
            }
            RegCloseKey(hKey);
        }
        return verified;
    }
};