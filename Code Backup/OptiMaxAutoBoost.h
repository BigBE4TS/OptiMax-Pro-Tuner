#pragma once
#include "OptiMaxEngine.h"
#include <string>
#include <vector>

namespace OptiMax {
    namespace AutoBoost {
        // Scans the background for your target game
        inline bool IsProcessRunning(const wchar_t* processName) {
            bool exists = false;
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap == INVALID_HANDLE_VALUE) return false;
            PROCESSENTRY32W pe = { sizeof(pe) };
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, processName) == 0) { exists = true; break; }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
            return exists;
        }

        // THE FIX: checking N configured target games used to mean N separate
        // CreateToolhelp32Snapshot calls (each walking the entire process list)
        // every single tick. With a library of several games that's redundant,
        // repeated work for no benefit - this takes one snapshot and checks it
        // against every target name in a single pass. Returns the matched name,
        // or an empty string if none of the targets are running.
        inline std::wstring FindRunningTarget(const std::vector<std::wstring>& targets) {
            if (targets.empty()) return L"";

            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap == INVALID_HANDLE_VALUE) return L"";

            std::wstring found = L"";
            PROCESSENTRY32W pe = { sizeof(pe) };
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    for (const auto& target : targets) {
                        if (_wcsicmp(pe.szExeFile, target.c_str()) == 0) {
                            found = target;
                            break;
                        }
                    }
                    if (!found.empty()) break;
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
            return found;
        }
    }
}