#pragma once
#include <windows.h>
#include <winioctl.h>
#include <string>
#include <wbemidl.h>
#include <comdef.h>
#include <algorithm>
#include "OptiMaxGlobals.h"
#include "OptiMaxText.h"   // ToUtf8 - WMI strings are vendor text, not guaranteed ASCII

#pragma comment(lib, "wbemuuid.lib")

namespace OptiMax {
    namespace Hardware {

        // THE FIX: Win32_DiskDrive's MediaType field almost universally reports
        // "Fixed hard disk media" for ANY fixed drive - HDD, SATA SSD, or NVMe -
        // because that WMI property was never updated to distinguish rotational
        // media. A substring match on "hard disk" therefore flags nearly every
        // system as having a mechanical drive. This queries the drive directly
        // for whether random-access seeks incur a penalty - true for HDDs, false
        // for SSDs/NVMe - the same technique Windows' own "Optimize Drives" tool
        // uses to decide whether to defrag or trim a given drive.
        inline bool DriveHasSeekPenalty(int driveIndex = 0) {
            wchar_t path[64];
            swprintf_s(path, L"\\\\.\\PhysicalDrive%d", driveIndex);

            HANDLE hDevice = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hDevice == INVALID_HANDLE_VALUE) return false; // can't tell - don't assume HDD

            STORAGE_PROPERTY_QUERY query = {};
            query.PropertyId = StorageDeviceSeekPenaltyProperty;
            query.QueryType = PropertyStandardQuery;

            DEVICE_SEEK_PENALTY_DESCRIPTOR desc = {};
            DWORD bytesReturned = 0;

            BOOL ok = DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
                &query, sizeof(query), &desc, sizeof(desc), &bytesReturned, NULL);

            CloseHandle(hDevice);

            if (!ok) return false; // query failed - don't assume HDD
            return desc.IncursSeekPenalty != FALSE;
        }

        // THE FIX: this runs on a detached std::thread (see OptiMaxApp.h). Nothing in
        // here was exception-safe, and several things in it throw: _bstr_t's constructor
        // raises _com_error on allocation failure, and std::wstring's constructor throws
        // on bad_alloc. An exception escaping the top of a detached thread does not
        // "fail the hardware scan" - it calls std::terminate and kills the whole app,
        // with no log line and no crash dialog. Same defect, and same fix, as the
        // CloudSync worker.
        inline void RunDeepWmiScanImpl();

        inline void RunDeepWmiScan() {
            try {
                RunDeepWmiScanImpl();
            }
            catch (...) {
                SafeAddLog("HARDWARE WARNING: The hardware scan failed unexpectedly. GPU and drive detection were skipped; everything else is unaffected.");
            }
        }

        inline void RunDeepWmiScanImpl() {
            HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
            if (FAILED(hres)) return;

            hres = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);

            IWbemLocator* pLoc = NULL;
            hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
            if (FAILED(hres)) { CoUninitialize(); return; }

            IWbemServices* pSvc = NULL;
            hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
            if (FAILED(hres)) { pLoc->Release(); CoUninitialize(); return; }

            hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

            // GPU SCAN (AMD DETECTION)
            IEnumWbemClassObject* pEnumerator = NULL;
            hres = pSvc->ExecQuery(_bstr_t("WQL"), _bstr_t("SELECT Name FROM Win32_VideoController"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);

            IWbemClassObject* pclsObj = NULL;
            ULONG uReturn = 0;
            while (pEnumerator) {
                hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                if (0 == uReturn) break;

                VARIANT vtProp;
                VariantInit(&vtProp);
                hres = pclsObj->Get(L"Name", 0, &vtProp, 0, 0);
                // bstrVal must be checked separately: a VT_BSTR variant is allowed to
                // carry a null BSTR, and std::wstring(nullptr) is undefined behaviour.
                if (SUCCEEDED(hres) && vtProp.vt == VT_BSTR && vtProp.bstrVal != nullptr) {
                    std::wstring ws(vtProp.bstrVal);
                    // The last of this project's iterator-range narrowings. A
                    // GPU name from WMI is vendor-supplied text, so it is not
                    // guaranteed ASCII - and this one feeds a substring match
                    // that decides whether AMD driver processes are protected
                    // from throttling.
                    std::string gpuName = OptiMax::Text::ToUtf8(ws);
                    std::transform(gpuName.begin(), gpuName.end(), gpuName.begin(), ::tolower);

                    if (gpuName.find("amd") != std::string::npos || gpuName.find("radeon") != std::string::npos) {
                        g_IsAmdGpu = true;
                        SafeAddLog("HARDWARE: AMD GPU detected. AMD driver processes added to the do-not-throttle list.");
                    }
                }
                // Outside the branch above: a variant of any other type still owns
                // resources, and clearing it only on the VT_BSTR path leaked the rest.
                VariantClear(&vtProp);
                pclsObj->Release();
            }
            // This was never released - one leaked WMI enumerator per run.
            if (pEnumerator) pEnumerator->Release();

            // DISK SCAN (LOW SPEC DETECTION) - direct drive query, no WMI needed here
            if (DriveHasSeekPenalty(0)) {
                g_IsLowSpec = true;
                SafeAddLog("HARDWARE: Mechanical hard drive detected. Automatic RAM cleaning disabled - on a slow disk it costs more in reads than it frees.");
            }

            pSvc->Release();
            pLoc->Release();
            CoUninitialize();
        }

        inline std::string GetCPUName() {
            HKEY hKey;
            char cpuName[256] = { 0 };
            // One byte short of the buffer, and terminated explicitly afterwards: the
            // registry does not guarantee a string value is null-terminated, and
            // std::string(cpuName) would read past the end if it isn't.
            DWORD bufSize = sizeof(cpuName) - 1;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                DWORD type = 0;
                if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, &type, (LPBYTE)cpuName, &bufSize) != ERROR_SUCCESS
                    || (type != REG_SZ && type != REG_EXPAND_SZ)) {
                    cpuName[0] = '\0';
                }
                else if (bufSize < sizeof(cpuName)) {
                    cpuName[bufSize] = '\0';
                }
                RegCloseKey(hKey);
            }
            std::string name(cpuName);
            size_t start = name.find_first_not_of(" ");
            if (start != std::string::npos) name = name.substr(start);
            return name.empty() ? "Unknown Processor" : name;
        }

        inline std::string GetRAMInfo() {
            // Zero-initialised and the result checked: on failure memInfo would
            // otherwise be uninitialised stack, and ullTotalPhys garbage - which feeds
            // the "less than 16GB" test that decides whether to disable RAM cleaning.
            MEMORYSTATUSEX memInfo = {};
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            if (!GlobalMemoryStatusEx(&memInfo)) memInfo.ullTotalPhys = 0;

            int gb = 0;
            if (memInfo.ullTotalPhys > 0) {
                gb = (int)((memInfo.ullTotalPhys + 536870912) / 1073741824);
                if (gb < 16) {
                    g_IsLowSpec = true;
                    SafeAddLog("HARDWARE: Less than 16GB of RAM. Automatic RAM cleaning disabled to avoid paging out data that's still in use.");
                }
            }

            // RESTORED: Original WMI Speed Query
            int speed = 0;
            HRESULT hres = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
            bool coInit = SUCCEEDED(hres);

            // THE FIX: this block was unguarded, and it is the same WMI work that
            // RunDeepWmiScan wraps in a try/catch - _bstr_t and _variant_t are the
            // compiler's COM helpers and they throw _com_error rather than returning
            // an HRESULT. The difference is where it runs: RunDeepWmiScan is on a
            // detached worker, but GetRAMInfo is called from the OptiMaxApp constructor
            // on the MAIN thread, before the window exists. A throw there escapes the
            // constructor and terminates the process at startup with no window and no
            // message - the identical silent-death failure the std::stoi comment in
            // OptiMaxApp.h describes fixing, one line below the call to this function.
            //
            // A missing memory speed is cosmetic; losing the whole application over it
            // is not. CoUninitialize is balanced on both paths.
            try {
                IWbemLocator* pLoc = NULL;
                if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc))) {
                    IWbemServices* pSvc = NULL;
                    if (SUCCEEDED(pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc))) {
                        CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
                        IEnumWbemClassObject* pEnumerator = NULL;
                        if (SUCCEEDED(pSvc->ExecQuery(_bstr_t("WQL"), _bstr_t("SELECT Speed FROM Win32_PhysicalMemory"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator))) {
                            IWbemClassObject* pclsObj = NULL;
                            ULONG uReturn = 0;
                            while (pEnumerator) {
                                pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                                if (0 == uReturn) break;

                                VARIANT vtProp;
                                VariantInit(&vtProp);

                                if (SUCCEEDED(pclsObj->Get(L"Speed", 0, &vtProp, 0, 0))) {
                                    if (vtProp.vt == VT_I4) speed = vtProp.intVal;
                                    else if (vtProp.vt == VT_UI4) speed = vtProp.uintVal;
                                    VariantClear(&vtProp);
                                }
                                pclsObj->Release();
                                if (speed > 0) break;
                            }
                            pEnumerator->Release();
                        }
                        pSvc->Release();
                    }
                    pLoc->Release();
                }
            }
            catch (...) {
                speed = 0;
                SafeAddLog("HARDWARE: Could not read the memory speed from WMI - the rest of the system information is unaffected.");
            }
            if (coInit) CoUninitialize();

            std::string res = gb > 0 ? std::to_string(gb) + " GB" : "Unknown RAM";
            if (speed > 0) res += " @ " + std::to_string(speed) + " MHz";
            return res;
        }
    }
}