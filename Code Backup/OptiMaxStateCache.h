#pragma once
#include <windows.h>
#include <string>
#include <map>

extern void SafeAddLog(const std::string& msg);

namespace OptiMax {
    namespace StateCache {
        // `inline`, not `static`: in a header, `static` would give every translation
        // unit its own private copy of the map, so a service cached from one .cpp
        // would be invisible to another. Harmless today (single .cpp) but it breaks
        // silently and confusingly the moment a second one is added.
        inline std::map<std::wstring, DWORD> OriginalServiceStates;
        inline const wchar_t* REGISTRY_CACHE_PATH = L"Software\\OptiMaxPro\\ServiceCache";

        // Reads a previously-persisted original state from the registry, if any.
        // Returns true and fills outState if found.
        inline bool LoadPersistedServiceState(const std::wstring& serviceName, DWORD& outState) {
            HKEY hKey;
            bool found = false;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_CACHE_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                DWORD val = 0, size = sizeof(DWORD);
                if (RegQueryValueExW(hKey, serviceName.c_str(), NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
                    outState = val;
                    found = true;
                }
                RegCloseKey(hKey);
            }
            return found;
        }

        // THE FIX: both the key creation and the write used to be unchecked. A failure
        // here is not cosmetic - it means the user's original value was never recorded,
        // which is precisely the loss the block comment below says this whole mechanism
        // exists to prevent. Silently recreating the bug it fixes is worse than the
        // original bug, because the log now looks clean while the data is gone.
        // Returns false, and says so, when the original could not be saved.
        inline bool PersistServiceState(const std::wstring& serviceName, DWORD state) {
            HKEY hKey;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_CACHE_PATH, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
                SafeAddLog("WARNING: Could not open OptiMax's settings cache to record an original "
                    "value - a later restore will fall back to Windows defaults instead of your "
                    "previous setting.");
                return false;
            }

            const LONG res = RegSetValueExW(hKey, serviceName.c_str(), 0, REG_DWORD, (const BYTE*)&state, sizeof(state));
            RegCloseKey(hKey);

            if (res != ERROR_SUCCESS) {
                SafeAddLog("WARNING: Could not record an original value to OptiMax's settings cache - "
                    "a later restore will fall back to Windows defaults instead of your previous setting.");
                return false;
            }
            return true;
        }

        inline void ClearPersistedServiceState(const std::wstring& serviceName) {
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_CACHE_PATH, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueW(hKey, serviceName.c_str());
                RegCloseKey(hKey);
            }
        }

        // THE FIX: this used to be an in-memory-only std::map. If the app crashed,
        // the user rebooted, or closed OptiMax between disabling a service and
        // restoring it, the true original state was lost forever - restore would
        // then guess SERVICE_AUTO_START for everything, silently overriding a
        // service the user had already set to Manual/Disabled themselves before
        // ever running OptiMax. Now it's persisted to the registry.
        //
        // Just as important: the registry is checked BEFORE querying the live
        // service state. Without that ordering, a second run (with the service
        // already disabled by OptiMax from a prior session) would "cache" the
        // already-disabled state as if it were the original, permanently losing
        // the real original state.
        inline void CacheService(const std::wstring& serviceName) {
            if (OriginalServiceStates.find(serviceName) != OriginalServiceStates.end()) return;

            DWORD persisted = 0;
            if (LoadPersistedServiceState(serviceName, persisted)) {
                OriginalServiceStates[serviceName] = persisted;
                return;
            }

            SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
            if (!hSCM) {
                // THE FIX: this path logged nothing at all. A total failure to reach the
                // service manager meant no original was cached, no warning was written,
                // and the first the user heard of it was a restore that put back a
                // default they never had.
                SafeAddLog("WARNING: Could not connect to the Windows service manager, so the original "
                    "startup type could not be recorded. A later restore will fall back to defaults.");
                return;
            }

            SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_QUERY_CONFIG);
            if (hSvc) {
                DWORD bytesNeeded = 0;
                QueryServiceConfigW(hSvc, NULL, 0, &bytesNeeded);
                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    LPQUERY_SERVICE_CONFIGW qsc = (LPQUERY_SERVICE_CONFIGW)malloc(bytesNeeded);

                    if (qsc) {
                        if (QueryServiceConfigW(hSvc, qsc, bytesNeeded, &bytesNeeded)) {
                            OriginalServiceStates[serviceName] = qsc->dwStartType;
                            PersistServiceState(serviceName, qsc->dwStartType);
                        }
                        else {
                            SafeAddLog("WARNING: Could not read a service's original startup type. A later "
                                "restore will fall back to defaults for it.");
                        }
                        free(qsc);
                    }
                }
                CloseServiceHandle(hSvc);
            }
            else {
                // THE FIX: this said "registry access may be restricted". The call that
                // failed was OpenServiceW - service-control access, not the registry -
                // so it pointed anyone debugging it at the wrong subsystem entirely.
                SafeAddLog("WARNING: Could not open a service to read its original startup type - "
                    "OptiMax may not have permission to manage it.");
            }
            CloseServiceHandle(hSCM);
        }

        // Generic DWORD-value cache aliases. Same backing store as the service cache
        // (HKCU\Software\OptiMaxPro\ServiceCache), used by the Tweaks page to record a
        // registry value's ORIGINAL contents so a tweak can be reversed to the user's
        // true prior value instead of a hardcoded default. Callers namespace their keys
        // (e.g. L"snappy::MenuShowDelay") to avoid colliding with service names.
        inline bool LoadPersistedValue(const std::wstring& key, DWORD& out) { return LoadPersistedServiceState(key, out); }
        inline bool PersistValue(const std::wstring& key, DWORD value) { return PersistServiceState(key, value); }
        inline void ClearPersistedValue(const std::wstring& key) { ClearPersistedServiceState(key); }

        // `fallback` is what to answer when nothing was ever cached for this service -
        // a fresh install restoring a tweak applied by a previous install, say. It used
        // to be hardcoded to SERVICE_AUTO_START, which is wrong for the majority of the
        // services this app touches: Fax, RetailDemo, MapsBroker, WpcMonSvc,
        // WMPNetworkSvc and dmwappushservice all ship as Manual, so "restoring" them
        // set them to Automatic and started services at every boot that Windows had
        // never started. Callers now pass the real stock value for their service.
        inline DWORD GetCachedServiceState(const std::wstring& serviceName, DWORD fallback = SERVICE_AUTO_START) {
            if (OriginalServiceStates.find(serviceName) != OriginalServiceStates.end()) {
                return OriginalServiceStates[serviceName];
            }
            DWORD persisted = 0;
            if (LoadPersistedServiceState(serviceName, persisted)) {
                return persisted;
            }
            return fallback;
        }

        // Call this once a service has been successfully restored to its original
        // state, so the cache doesn't linger and interfere with a future session
        // where the user might legitimately change that service's startup type
        // themselves outside of OptiMax.
        inline void ClearCachedServiceState(const std::wstring& serviceName) {
            OriginalServiceStates.erase(serviceName);
            ClearPersistedServiceState(serviceName);
        }
    }
}