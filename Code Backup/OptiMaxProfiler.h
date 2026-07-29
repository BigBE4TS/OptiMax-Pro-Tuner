#pragma once
#include "OptiMaxEngine.h"

namespace OptiMax {
    namespace Profiler {

        inline double GetMaxDPCSpikeMicroseconds() {
            LARGE_INTEGER freq, start, end;
            QueryPerformanceFrequency(&freq);
            double maxSpike = 0.0;

            HANDLE hThread = GetCurrentThread();
            int oldPriority = GetThreadPriority(hThread);
            SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL);

            QueryPerformanceCounter(&start);
            for (int i = 0; i < 10000; i++) {
                QueryPerformanceCounter(&end);
                double deltaUs = ((double)(end.QuadPart - start.QuadPart) * 1000000.0) / freq.QuadPart;

                if (deltaUs > maxSpike) {
                    maxSpike = deltaUs;
                }
                start = end;
            }

            SetThreadPriority(hThread, oldPriority);

            return maxSpike;
        }

        inline double GetCurrentTimerResolution() {
            ULONG minRes, maxRes, currentRes = 0;
            NtQueryTimerResolution(&minRes, &maxRes, &currentRes);
            return (double)currentRes / 10000.0;
        }

        inline int GetRAMUsage() {
            // Zero-initialised and checked: this value drives the automatic RAM
            // cleaner's threshold test, and on a failed call it was previously
            // whatever happened to be on the stack - which could read as 90%+ and
            // trigger a purge for no reason.
            MEMORYSTATUSEX memInfo = {};
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            if (!GlobalMemoryStatusEx(&memInfo)) return 0;
            return (int)memInfo.dwMemoryLoad;
        }

        // THE FIX: the Settings toggle for Multi-Plane Overlay saved a preference,
        // logged that it had been applied, and stopped there. Nothing in the whole
        // program ever wrote OverlayTestMode, so MPO was never actually disabled
        // for anyone - the toggle did nothing but remember its own position.
        // The only other code that touched the value was a reader, IsMPODisabled(),
        // which nothing called either - so the lie was never even visible. That
        // reader has since been removed; this writer is the whole feature.
        //
        // 5 is the value DWM checks to bypass overlay planes. It is read when DWM
        // starts, so the change lands on the next reboot - callers must say so
        // rather than reporting an instant fix that the user will not observe.
        inline bool SetMPODisabled(bool disable) {
            HKEY hKey;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\Dwm",
                0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
                return false;
            }

            LONG res;
            if (disable) {
                DWORD v = 0x00000005;
                res = RegSetValueExW(hKey, L"OverlayTestMode", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
            }
            else {
                res = RegDeleteValueW(hKey, L"OverlayTestMode");
                // Turning off something that was never on is a success, not a failure.
                if (res == ERROR_FILE_NOT_FOUND) res = ERROR_SUCCESS;
            }

            RegCloseKey(hKey);
            return res == ERROR_SUCCESS;
        }
    }
}