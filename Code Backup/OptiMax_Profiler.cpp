// --- CRITICAL FIX FOR THE 180 NETWORK ERRORS ---
#define WIN32_LEAN_AND_MEAN 
// -----------------------------------------------

#define NOMINMAX
#define IMGUI_DEFINE_MATH_OPERATORS 

#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")

#pragma warning(disable: 4005)
#define WIN32_NO_STATUS 
#include <windows.h>
#undef WIN32_NO_STATUS
#include <shellapi.h>
#include <dwmapi.h>
#include <sstream>
#include <fstream>
#include <map>

#include "resource.h" 
#include "OptiMaxApp.h"
#include "OptiMaxGlobals.h" 

// --- LINKER INSTRUCTIONS ---
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

// --- DWM DARK MODE MACROS ---
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

// ========================================================================
// PHYSICAL MEMORY ALLOCATION FOR GLOBALS (MUST BE OUTSIDE ANY FUNCTION)
// ========================================================================

// --- HARDWARE PROFILE GLOBALS ---
bool g_IsAmdGpu = false;
bool g_IsLowSpec = false;
bool g_IsHeterogeneousCpu = false;

std::atomic<bool> g_isAppRunning{ true };
std::atomic<bool> g_isDownloadingUpdate{ false };
std::atomic<bool> g_showCloseConfirm{ false };
std::atomic<bool> g_askAutoStartOnFirstRun{ false };
std::atomic<bool> g_autoStartAsked{ false };
std::atomic<int> g_closeBehavior{ 0 }; // 0 = ask every time, 1 = always tray, 2 = always exit
std::atomic<bool> g_isAntiCheatActive{ false };
std::atomic<bool> g_isGameRunning{ false };
std::atomic<bool> g_isCoreActive{ false };
std::atomic<bool> g_autoBoostEnabled{ false };

// --- TRUE PERSISTENT UI STATES ---
std::atomic<bool> g_coresUnparked{ false };
std::atomic<bool> g_bootOptimized{ false };
std::atomic<bool> g_servicesSanitized{ false };
std::atomic<bool> g_privacyTweaked{ false };
std::atomic<bool> g_extendedServices{ false };
std::atomic<bool> g_telemetryTasks{ false };
std::atomic<bool> g_snappyUI{ false };
std::atomic<bool> g_mouseAccelDisabled{ false };
std::atomic<bool> g_gameDvrDisabled{ false };
std::atomic<bool> g_mmcssGames{ false };
std::atomic<bool> g_globalTimerRes{ false };
std::atomic<bool> g_isolationActive{ false };
std::atomic<bool> g_pCoresLocked{ false };
// THE FEATURE: per-game override for P-core locking. A game not present in this
// map falls back to the global g_pCoresLocked default (so existing behavior is
// unchanged for anyone who hasn't set a per-game override). Guarded by
// g_configMutex, same as g_targetGames, since they're always edited together.
std::map<std::string, bool> g_gamePCoreOverrides;

// THE FEATURE: four more per-game flags, in the same shape as the P-core
// override map above and guarded by the same g_configMutex. A game with no
// entry in a given map behaves exactly as it did before these existed, so
// nothing changes for anyone who never sets one.
//
// These are what a community profile carries (see OptiMaxCloudSync.h), but they
// are plain local settings: a profile only ever writes into these maps as a
// direct result of the user clicking "Add to My Library", never automatically.
std::map<std::string, bool> g_gameDisableSMT;
std::map<std::string, bool> g_gameHighPriority;
std::map<std::string, bool> g_gameAggressiveRAMPurge;
std::map<std::string, bool> g_gameDisableFSO;

std::atomic<bool> g_msiEnabled{ false };
std::atomic<bool> g_networkTweaked{ false };
std::atomic<bool> g_fseForced{ false };

// --- ADVANCED TWEAK STATES ---
std::atomic<bool> g_vbsDisabled{ false };
std::atomic<bool> g_irqPinned{ false };

// --- MIGRATED SETTINGS TO REGISTRY ---
std::atomic<bool> g_settingAutoStart{ false };
std::atomic<bool> g_settingSafeMode{ true };
// Defaults to off. It defaulted to ON while the toggle was inert, which was
// harmless only because nothing was applied. Now that flipping it genuinely
// rewrites a DWM setting, defaulting it on would silently change the graphics
// behaviour of every fresh install - and disabling overlay planes is a fix for
// a specific flickering problem, not a free win. Existing saved preferences are
// untouched; this only decides what a new install starts with.
std::atomic<bool> g_settingMpoDisable{ false };
std::atomic<bool> g_settingAutoRam{ false };
std::atomic<int> g_ramCleanThreshold{ 75 }; // RAM usage % that triggers an automatic clean
// ---------------------------------

std::mutex g_configMutex;
std::vector<std::string> g_targetGames;

std::atomic<bool> g_isBenchmarking{ false };
std::mutex g_benchMutex;
OptiMax::Benchmark::AuditResult g_lastAudit = { 0.0, 0.0, 0.0 };

ULONGLONG g_benchStartTime = 0;
std::thread g_benchThread;

std::mutex g_logMutex;
std::vector<std::string> g_logs;

NOTIFYICONDATAW g_nid = {};
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// --- FORWARD DECLARATIONS ---
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ===========================================================================
// ROLLING LOG FILE
//
// g_logs is an in-memory ring buffer of 500 lines that dies with the process and
// can only be read through the GUI. That means a user reporting a bug has no way
// to send you anything, and a crash takes its own explanation with it - which has
// already cost real answers more than once (an instance that exited overnight left
// nothing behind to say why).
//
// Every SafeAddLog line is mirrored here. Notes on the choices:
//   - Written next to the exe so it is trivial to describe over chat ("the .log
//     next to OptiMax_Profiler.exe"), with a %LOCALAPPDATA% fallback because the
//     exe can sit somewhere a standard user cannot write.
//   - flush() after every line. Buffered output is empty in exactly the case the
//     log exists for, which is the process dying unexpectedly.
//   - Two files, current + .1, capped at 2 MB each. Bounded on disk without
//     throwing away the previous session, which is usually the one being reported.
//   - Every failure path is silent. A logging problem must never become a second
//     bug on top of the one being diagnosed.
// ===========================================================================
static const long long kLogMaxBytes = 2 * 1024 * 1024;
static std::ofstream g_logFile;
static std::wstring g_logFilePath;
static long long g_logBytes = 0;
static bool g_logInitTried = false;

static std::wstring LogDirectory() {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
        std::wstring p(exePath);
        size_t slash = p.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            std::wstring dir = p.substr(0, slash);
            // Probe rather than assume: FILE_FLAG_DELETE_ON_CLOSE means the test
            // file removes itself even if we are killed between the two calls.
            std::wstring probe = dir + L"\\.optimax_write_test";
            HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
            if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return dir; }
        }
    }
    wchar_t local[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring dir(local);
        dir += L"\\OptiMax";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir;
    }
    return L"";
}

// Deliberately a raw token query rather than IsUserAnAdmin(): that lives in
// shlobj.h, which this file only receives because OptiMaxApp.h happens to include
// OptiMaxDiscord.h first. Relying on that is the exact pattern that hid the
// missing <shellapi.h> in OptiMaxEngine.h.
static bool IsElevatedProcess() {
    HANDLE hToken = NULL;
    bool elevated = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION te = {};
        DWORD cb = sizeof(te);
        if (GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &cb)) {
            elevated = (te.TokenIsElevated != 0);
        }
        CloseHandle(hToken);
    }
    return elevated;
}

static std::string DescribeWindows() {
    HKEY hKey;
    std::string out = "unknown";
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[128]; DWORD size; DWORD type = 0;
        std::string display, build;
        size = sizeof(buf) - 1;
        if (RegQueryValueExA(hKey, "DisplayVersion", NULL, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS && type == REG_SZ) {
            buf[size] = '\0'; display = buf;
        }
        size = sizeof(buf) - 1; type = 0;
        if (RegQueryValueExA(hKey, "CurrentBuild", NULL, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS && type == REG_SZ) {
            buf[size] = '\0'; build = buf;
        }
        DWORD ubr = 0, ubrSize = sizeof(ubr); type = 0;
        RegQueryValueExA(hKey, "UBR", NULL, &type, (LPBYTE)&ubr, &ubrSize);
        RegCloseKey(hKey);
        if (!build.empty()) {
            out = "build " + build;
            if (ubr) out += "." + std::to_string(ubr);
            if (!display.empty()) out = display + " (" + out + ")";
        }
    }
    return out;
}

// Caller must hold g_logMutex.
static void OpenLogFileLocked() {
    g_logInitTried = true;
    std::wstring dir = LogDirectory();
    if (dir.empty()) return;

    g_logFilePath = dir + L"\\OptiMax_Profiler.log";

    // Rotate on open rather than mid-session, so one run's output is never split
    // across two files - which is the thing that makes a report hard to read.
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(g_logFilePath.c_str(), GetFileExInfoStandard, &fad)) {
        long long size = ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        if (size >= kLogMaxBytes) {
            std::wstring prev = dir + L"\\OptiMax_Profiler.1.log";
            MoveFileExW(g_logFilePath.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }

    g_logFile.open(g_logFilePath, std::ios::app | std::ios::binary);
    if (!g_logFile) return;

    g_logBytes = (long long)g_logFile.tellp();
    if (g_logBytes < 0) g_logBytes = 0;

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring wexe(exePath);

    SYSTEMTIME st; GetLocalTime(&st);
    char stamp[64];
    sprintf_s(stamp, "%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::ostringstream hdr;
    hdr << "\r\n===== OptiMax session started " << stamp << " =====\r\n"
        << "exe      : " << std::string(wexe.begin(), wexe.end()) << "\r\n"
        << "windows  : " << DescribeWindows() << "\r\n"
        << "elevated : " << (IsElevatedProcess() ? "yes" : "no") << "\r\n";
    std::string h = hdr.str();
    g_logFile << h;
    g_logFile.flush();
    g_logBytes += (long long)h.size();
}

// Caller must hold g_logMutex.
static void WriteLogLineLocked(const std::string& line) {
    if (!g_logInitTried) OpenLogFileLocked();
    if (!g_logFile.is_open()) return;

    g_logFile << line << "\r\n";
    g_logFile.flush();
    g_logBytes += (long long)line.size() + 2;

    if (g_logBytes >= kLogMaxBytes) {
        g_logFile.close();
        g_logInitTried = false;   // next line reopens, rotating on the way
        g_logBytes = 0;
    }
}

void SafeAddLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeBuf[64];
    sprintf_s(timeBuf, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

    std::string line = std::string(timeBuf) + msg;
    g_logs.push_back(line);

    if (g_logs.size() > 500) {
        g_logs.erase(g_logs.begin());
    }

    // Mirrored to disk so the log outlives the process and can be attached to a
    // bug report. The in-memory ring above is unchanged - the GUI still reads it.
    WriteLogLineLocked(line);
}

// Where the log ended up, for the GUI to show and for the user to quote. Empty
// if the file could not be opened anywhere.
std::wstring GetLogFilePath() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_logFile.is_open() ? g_logFilePath : std::wstring();
}

// THE FEATURE: the "name:0|name:1" encoding the P-core override map already
// used, factored out now that five per-game flag maps need exactly the same
// treatment in four different places (registry save, registry load, profile
// export, profile import). The encoding itself is unchanged, so existing
// PCoreOverrides registry values and .optiprofile files still round-trip.
static std::string SerializeBoolMap(const std::map<std::string, bool>& source) {
    std::string out;
    bool first = true;
    for (const auto& kv : source) {
        if (!first) out += "|";
        out += kv.first + ":" + (kv.second ? "1" : "0");
        first = false;
    }
    return out;
}

// Caller is responsible for holding g_configMutex - every call site here
// already does, since these maps are always edited alongside g_targetGames.
static void DeserializeBoolMap(const std::string& value, std::map<std::string, bool>& dest) {
    dest.clear();

    std::string s = value;
    size_t pos = 0;
    while ((pos = s.find("|")) != std::string::npos) {
        std::string token = s.substr(0, pos);
        s.erase(0, pos + 1);
        size_t colon = token.find(":");
        // colon > 0 rejects a ":1" entry with an empty game name, which would
        // otherwise become a map key that can never match a real process.
        if (colon != std::string::npos && colon > 0) {
            dest[token.substr(0, colon)] = (token.substr(colon + 1) == "1");
        }
    }
    if (!s.empty()) {
        size_t colon = s.find(":");
        if (colon != std::string::npos && colon > 0) {
            dest[s.substr(0, colon)] = (s.substr(colon + 1) == "1");
        }
    }
}

void SavePersistentState() {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\OptiMaxPro", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val;

        val = g_isCoreActive.load() ? 1 : 0;
        RegSetValueExA(hKey, "CoreTweaks", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_autoBoostEnabled.load() ? 1 : 0;
        RegSetValueExA(hKey, "AutoBoost", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_coresUnparked.load() ? 1 : 0;
        RegSetValueExA(hKey, "CoresUnparked", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_bootOptimized.load() ? 1 : 0;
        RegSetValueExA(hKey, "BootOptimized", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_servicesSanitized.load() ? 1 : 0;
        RegSetValueExA(hKey, "ServicesSanitized", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_privacyTweaked.load() ? 1 : 0;
        RegSetValueExA(hKey, "PrivacyTweaked", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_extendedServices.load() ? 1 : 0;
        RegSetValueExA(hKey, "ExtendedServices", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_telemetryTasks.load() ? 1 : 0;
        RegSetValueExA(hKey, "TelemetryTasks", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_snappyUI.load() ? 1 : 0;
        RegSetValueExA(hKey, "SnappyUI", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_mouseAccelDisabled.load() ? 1 : 0;
        RegSetValueExA(hKey, "MouseAccelDisabled", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_gameDvrDisabled.load() ? 1 : 0;
        RegSetValueExA(hKey, "GameDvrDisabled", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_mmcssGames.load() ? 1 : 0;
        RegSetValueExA(hKey, "MmcssGames", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_globalTimerRes.load() ? 1 : 0;
        RegSetValueExA(hKey, "GlobalTimerRes", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_isolationActive.load() ? 1 : 0;
        RegSetValueExA(hKey, "IsolationActive", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_pCoresLocked.load() ? 1 : 0;
        RegSetValueExA(hKey, "PCoresLocked", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_msiEnabled.load() ? 1 : 0;
        RegSetValueExA(hKey, "MSIEnabled", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_networkTweaked.load() ? 1 : 0;
        RegSetValueExA(hKey, "NetworkTweaked", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_fseForced.load() ? 1 : 0;
        RegSetValueExA(hKey, "FseForced", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_vbsDisabled.load() ? 1 : 0;
        RegSetValueExA(hKey, "VbsDisabled", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_irqPinned.load() ? 1 : 0;
        RegSetValueExA(hKey, "IrqPinned", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_settingAutoStart.load() ? 1 : 0;
        RegSetValueExA(hKey, "AutoStart", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        // Whether the "Start with Windows?" question has been put to the user.
        // Separate from the answer, so declining is remembered as a decision
        // rather than looking identical to never having been asked.
        val = g_autoStartAsked.load() ? 1 : 0;
        RegSetValueExA(hKey, "AutoStartAsked", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_settingSafeMode.load() ? 1 : 0;
        RegSetValueExA(hKey, "SafeMode", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_settingMpoDisable.load() ? 1 : 0;
        RegSetValueExA(hKey, "MpoDisable", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_settingAutoRam.load() ? 1 : 0;
        RegSetValueExA(hKey, "AutoRam", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = (DWORD)g_ramCleanThreshold.load();
        RegSetValueExA(hKey, "RamThreshold", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = (DWORD)g_closeBehavior.load();
        RegSetValueExA(hKey, "CloseBehavior", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        std::string joinedLib = "";
        std::string joinedOverrides = "";
        std::string joinedSmt = "";
        std::string joinedPriority = "";
        std::string joinedRamPurge = "";
        std::string joinedFso = "";
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            for (size_t i = 0; i < g_targetGames.size(); ++i) {
                joinedLib += g_targetGames[i];
                if (i < g_targetGames.size() - 1) joinedLib += "|";
            }
            joinedOverrides = SerializeBoolMap(g_gamePCoreOverrides);
            joinedSmt = SerializeBoolMap(g_gameDisableSMT);
            joinedPriority = SerializeBoolMap(g_gameHighPriority);
            joinedRamPurge = SerializeBoolMap(g_gameAggressiveRAMPurge);
            joinedFso = SerializeBoolMap(g_gameDisableFSO);
        }
        RegSetValueExA(hKey, "Library", 0, REG_SZ, (const BYTE*)joinedLib.c_str(), (DWORD)(joinedLib.size() + 1));
        RegSetValueExA(hKey, "PCoreOverrides", 0, REG_SZ, (const BYTE*)joinedOverrides.c_str(), (DWORD)(joinedOverrides.size() + 1));
        RegSetValueExA(hKey, "GameDisableSMT", 0, REG_SZ, (const BYTE*)joinedSmt.c_str(), (DWORD)(joinedSmt.size() + 1));
        RegSetValueExA(hKey, "GameHighPriority", 0, REG_SZ, (const BYTE*)joinedPriority.c_str(), (DWORD)(joinedPriority.size() + 1));
        RegSetValueExA(hKey, "GameAggressiveRAMPurge", 0, REG_SZ, (const BYTE*)joinedRamPurge.c_str(), (DWORD)(joinedRamPurge.size() + 1));
        RegSetValueExA(hKey, "GameDisableFSO", 0, REG_SZ, (const BYTE*)joinedFso.c_str(), (DWORD)(joinedFso.size() + 1));
        RegCloseKey(hKey);
    }
}

bool LoadPersistentState() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\OptiMaxPro", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0;
        DWORD size = sizeof(DWORD);

        if (RegQueryValueExA(hKey, "CoreTweaks", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_isCoreActive = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "AutoBoost", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_autoBoostEnabled = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "CoresUnparked", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_coresUnparked = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "BootOptimized", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_bootOptimized = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "ServicesSanitized", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_servicesSanitized = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "PrivacyTweaked", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_privacyTweaked = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "ExtendedServices", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_extendedServices = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "TelemetryTasks", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_telemetryTasks = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "SnappyUI", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_snappyUI = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "MouseAccelDisabled", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_mouseAccelDisabled = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "GameDvrDisabled", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_gameDvrDisabled = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "MmcssGames", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_mmcssGames = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "GlobalTimerRes", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_globalTimerRes = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "IsolationActive", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_isolationActive = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "PCoresLocked", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_pCoresLocked = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "MSIEnabled", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_msiEnabled = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "NetworkTweaked", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_networkTweaked = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "FseForced", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_fseForced = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "VbsDisabled", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_vbsDisabled = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "IrqPinned", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_irqPinned = (val == 1);

        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "AutoStart", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_settingAutoStart = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "AutoStartAsked", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_autoStartAsked = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "SafeMode", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_settingSafeMode = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "MpoDisable", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_settingMpoDisable = (val == 1);
        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "AutoRam", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) g_settingAutoRam = (val == 1);

        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "RamThreshold", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            // Clamp to a sane range - too low causes constant thrashing, too high defeats the point.
            int clamped = (int)val;
            if (clamped < 50) clamped = 50;
            if (clamped > 95) clamped = 95;
            g_ramCleanThreshold = clamped;
        }

        size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "CloseBehavior", NULL, NULL, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            int cb = (int)val;
            if (cb >= 0 && cb <= 2) g_closeBehavior = cb;
        }

        // THE FIX: every string value here used to be read into a fixed stack buffer
        // (2048 for the library, 4096 for the flag maps). RegQueryValueExA does not
        // truncate - it fails with ERROR_MORE_DATA and copies nothing - so a library
        // that outgrew the buffer did not load partially, it did not load AT ALL. The
        // globals kept their empty defaults, and the next SavePersistentState() (which
        // any toggle in the UI triggers) wrote that empty library straight back over
        // the real one. A big enough game library silently deleted itself. There is
        // also no guarantee a registry string value is null-terminated, which
        // `std::string s = gameBuf;` assumed. Ask for the size first, allocate for it,
        // and terminate explicitly.
        auto readRegString = [&](const char* valueName, std::string& out) -> bool {
            out.clear();
            DWORD type = 0, needed = 0;
            if (RegQueryValueExA(hKey, valueName, NULL, &type, NULL, &needed) != ERROR_SUCCESS) return false;
            if (type != REG_SZ && type != REG_EXPAND_SZ) return false;
            if (needed == 0) return true; // present but empty
            std::vector<char> buf(needed + 1, '\0');
            DWORD size = needed;
            if (RegQueryValueExA(hKey, valueName, NULL, NULL, (LPBYTE)buf.data(), &size) != ERROR_SUCCESS) return false;
            if (size > needed) size = needed;
            buf[size] = '\0';
            out.assign(buf.data());
            return true;
            };

        std::string libraryValue;
        if (readRegString("Library", libraryValue)) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            g_targetGames.clear();
            std::string s = libraryValue;
            size_t pos = 0;
            std::string token;
            while ((pos = s.find("|")) != std::string::npos) {
                token = s.substr(0, pos);
                if (!token.empty()) g_targetGames.push_back(token);
                s.erase(0, pos + 1);
            }
            if (!s.empty()) g_targetGames.push_back(s);
        }

        // All five per-game flag maps share one loader.
        auto loadBoolMapValue = [&](const char* valueName, std::map<std::string, bool>& dest) {
            std::string value;
            if (readRegString(valueName, value)) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                DeserializeBoolMap(value, dest);
            }
            };

        loadBoolMapValue("PCoreOverrides", g_gamePCoreOverrides);
        loadBoolMapValue("GameDisableSMT", g_gameDisableSMT);
        loadBoolMapValue("GameHighPriority", g_gameHighPriority);
        loadBoolMapValue("GameAggressiveRAMPurge", g_gameAggressiveRAMPurge);
        loadBoolMapValue("GameDisableFSO", g_gameDisableFSO);

        RegCloseKey(hKey);
        return true;
    }
    return false;
}

void SetStartupRegistry(bool enable);

// THE FEATURE: exports the user-facing settings (not transient runtime state
// like "is core active right now" - those get re-derived at boot) to a plain
// text file, and imports them back. Simple Key=Value format rather than JSON,
// matching the rest of this codebase's preference for straightforward
// registry/string-based persistence over pulling in a parsing dependency.
bool ExportProfile(const std::wstring& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "[OptiMax Profile]\n";
    file << "Version=4.2.0\n";
    file << "AutoStart=" << (g_settingAutoStart.load() ? 1 : 0) << "\n";
    file << "SafeMode=" << (g_settingSafeMode.load() ? 1 : 0) << "\n";
    file << "MpoDisable=" << (g_settingMpoDisable.load() ? 1 : 0) << "\n";
    file << "AutoRam=" << (g_settingAutoRam.load() ? 1 : 0) << "\n";
    file << "RamThreshold=" << g_ramCleanThreshold.load() << "\n";
    file << "CloseBehavior=" << g_closeBehavior.load() << "\n";
    file << "PCoresLocked=" << (g_pCoresLocked.load() ? 1 : 0) << "\n";

    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        file << "Library=";
        for (size_t i = 0; i < g_targetGames.size(); ++i) {
            file << g_targetGames[i];
            if (i < g_targetGames.size() - 1) file << "|";
        }
        file << "\n";

        // Existing field keys are unchanged so previously exported profiles
        // still import; the four Game* keys below are new additions.
        file << "PCoreOverrides=" << SerializeBoolMap(g_gamePCoreOverrides) << "\n";
        file << "GameDisableSMT=" << SerializeBoolMap(g_gameDisableSMT) << "\n";
        file << "GameHighPriority=" << SerializeBoolMap(g_gameHighPriority) << "\n";
        file << "GameAggressiveRAMPurge=" << SerializeBoolMap(g_gameAggressiveRAMPurge) << "\n";
        file << "GameDisableFSO=" << SerializeBoolMap(g_gameDisableFSO) << "\n";
    }

    file.close();
    SafeAddLog("BACKUP: Settings and game library saved to file.");
    return true;
}

bool ImportProfile(const std::wstring& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SafeAddLog("BACKUP ERROR: Could not open the selected file.");
        return false;
    }

    // THE FIX: this used to apply every key to the live globals as it parsed, and
    // only check for the [OptiMax Profile] header AFTER the loop had finished. So
    // pointing Import at any text file with Key=Value lines - or at a truncated or
    // half-written profile - overwrote the running settings and wiped the game
    // library, then returned an error saying nothing had been imported. The state
    // was already gone, and the next SavePersistentState() from any UI action
    // committed the damage to the registry. Nothing is applied until the file has
    // been read in full and confirmed to be an OptiMax profile.
    std::vector<std::pair<std::string, std::string>> entries;
    std::string line;
    bool sawHeader = false;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line == "[OptiMax Profile]") { sawHeader = true; continue; }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        entries.emplace_back(line.substr(0, eq), line.substr(eq + 1));
    }
    file.close();

    if (!sawHeader) {
        SafeAddLog("BACKUP ERROR: That file doesn't look like an OptiMax settings file. Nothing was changed.");
        return false;
    }

    for (const auto& entry : entries) {
        const std::string& key = entry.first;
        const std::string& value = entry.second;

        // Records the preference only. Acting on it here re-registered the logon
        // task on every launch with the running copy's path - see SyncStartupTask,
        // which now does it once, and only when something has actually changed.
        if (key == "AutoStart") { g_settingAutoStart = (value == "1"); }
        else if (key == "SafeMode") g_settingSafeMode = (value == "1");
        else if (key == "MpoDisable") g_settingMpoDisable = (value == "1");
        else if (key == "AutoRam") g_settingAutoRam = (value == "1");
        else if (key == "RamThreshold") {
            try { int t = std::stoi(value); if (t >= 50 && t <= 95) g_ramCleanThreshold = t; }
            catch (...) {}
        }
        else if (key == "CloseBehavior") {
            try { int cb = std::stoi(value); if (cb >= 0 && cb <= 2) g_closeBehavior = cb; }
            catch (...) {}
        }
        else if (key == "PCoresLocked") g_pCoresLocked = (value == "1");
        else if (key == "Library") {
            std::lock_guard<std::mutex> lock(g_configMutex);
            g_targetGames.clear();
            std::string s = value;
            size_t pos = 0;
            while ((pos = s.find("|")) != std::string::npos) {
                std::string token = s.substr(0, pos);
                if (!token.empty()) g_targetGames.push_back(token);
                s.erase(0, pos + 1);
            }
            if (!s.empty()) g_targetGames.push_back(s);
        }
        else if (key == "PCoreOverrides") {
            std::lock_guard<std::mutex> lock(g_configMutex);
            DeserializeBoolMap(value, g_gamePCoreOverrides);
        }
        else if (key == "GameDisableSMT") {
            std::lock_guard<std::mutex> lock(g_configMutex);
            DeserializeBoolMap(value, g_gameDisableSMT);
        }
        else if (key == "GameHighPriority") {
            std::lock_guard<std::mutex> lock(g_configMutex);
            DeserializeBoolMap(value, g_gameHighPriority);
        }
        else if (key == "GameAggressiveRAMPurge") {
            std::lock_guard<std::mutex> lock(g_configMutex);
            DeserializeBoolMap(value, g_gameAggressiveRAMPurge);
        }
        else if (key == "GameDisableFSO") {
            std::lock_guard<std::mutex> lock(g_configMutex);
            DeserializeBoolMap(value, g_gameDisableFSO);
        }
    }

    SavePersistentState();
    SafeAddLog("BACKUP: Settings and game library loaded from file.");
    return true;
}

// The path the logon task was last registered with. Kept so the task can be
// re-registered only when doing so would actually change something.
static const wchar_t* kTaskPathValue = L"StartupTaskPath";

static std::wstring ReadRecordedTaskPath() {
    HKEY hKey;
    std::wstring result;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\OptiMaxPro", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {};
        // One wchar_t short of the buffer so a registry value that isn't
        // null-terminated (the registry does not guarantee it) still can't run
        // `result = buf` off the end.
        DWORD size = sizeof(buf) - sizeof(wchar_t);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, kTaskPathValue, NULL, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS
            && (type == REG_SZ || type == REG_EXPAND_SZ)) {
            buf[size / sizeof(wchar_t)] = L'\0';
            result = buf;
        }
        RegCloseKey(hKey);
    }
    return result;
}

static void WriteRecordedTaskPath(const std::wstring& path) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\OptiMaxPro", 0, NULL, 0,
        KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        if (path.empty()) {
            RegDeleteValueW(hKey, kTaskPathValue);
        }
        else {
            RegSetValueExW(hKey, kTaskPathValue, 0, REG_SZ,
                (const BYTE*)path.c_str(), (DWORD)((path.size() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(hKey);
    }
}

// Case-insensitive substring test. Deliberately hand-rolled and ASCII-only rather
// than pulling in <algorithm>/<cctype>: every caller passes a lowercase ASCII path
// fragment, and this file must not depend on an include that only arrives because
// OptiMaxApp.h happened to pull it in first (see the missing <shellapi.h> in
// OptiMaxEngine.h, which compiled for months purely by include-order luck).
static bool ContainsNoCase(const std::wstring& haystack, const wchar_t* lowerNeedle) {
    std::wstring h;
    h.reserve(haystack.size());
    for (wchar_t c : haystack) h += (c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c;
    return h.find(lowerNeedle) != std::wstring::npos;
}

static bool PathExistsW(const std::wstring& p) {
    return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// True if this path is a build output rather than an installed copy.
//
// Detection is by ARTIFACT, not by folder name: a CMake / Ninja / MSBuild output
// directory always has its generator's bookkeeping sitting next to the binary, and
// that is true whatever the directory is called. The literal "\out\build\" test is
// only a fallback for Visual Studio's default CMake layout.
//
// Both the exe's own directory and its parent are checked, because a multi-config
// generator adds a Release\ or Debug\ level below the directory holding
// CMakeCache.txt.
static bool IsBuildOutputCopy(const std::wstring& exePath) {
    static const wchar_t* kMarkers[] = {
        L"\\CMakeCache.txt", L"\\build.ninja", L"\\CMakeFiles", L"\\ALL_BUILD.vcxproj"
    };

    size_t slash = exePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    std::wstring dir = exePath.substr(0, slash);

    for (auto m : kMarkers) if (PathExistsW(dir + m)) return true;

    size_t up = dir.find_last_of(L"\\/");
    if (up != std::wstring::npos) {
        std::wstring parent = dir.substr(0, up);
        for (auto m : kMarkers) if (PathExistsW(parent + m)) return true;
    }

    return ContainsNoCase(exePath, L"\\out\\build\\");
}

// Runs schtasks.exe hidden and returns its exit code, or -1 if it could not be
// started or did not finish in time.
//
// THE FIX: SetStartupRegistry() used ShellExecuteW and tested the result > 32,
// which only reports that the process was LAUNCHED - never whether schtasks
// actually did anything. A refused registration therefore still recorded the path
// as though it had worked, so the recorded path could name a task that does not
// exist. That matters more than a wrong log line: SyncStartupTask() trusts the
// recorded path, so a silent failure left it believing the startup entry was
// correct and it would never retry.
static int RunSchtasks(const std::wstring& params) {
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = L"schtasks.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || !sei.hProcess) return -1;

    DWORD code = (DWORD)-1;
    // schtasks returns in well under a second; the timeout only exists so a wedged
    // process can never hang startup or the settings toggle.
    if (WaitForSingleObject(sei.hProcess, 10000) == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(sei.hProcess, &code)) code = (DWORD)-1;
    }
    CloseHandle(sei.hProcess);
    return (int)code;
}

void SetStartupRegistry(bool enable) {
    wchar_t szPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, szPath, MAX_PATH)) {
        if (enable) {
            std::wstring params = L"/create /tn \"OptiMaxPro\" /tr \"\\\"";
            params += szPath;
            params += L"\\\" -tray\" /sc onlogon /rl highest /f";

            if (RunSchtasks(params) == 0) {
                WriteRecordedTaskPath(szPath);
                SafeAddLog("SYSTEM: OptiMax will now start with Windows, minimized to the tray.");
            }
            else {
                SafeAddLog("SYSTEM WARNING: Could not set OptiMax to start with Windows - administrator rights are required.");
            }
        }
        else {
            if (RunSchtasks(L"/delete /tn \"OptiMaxPro\" /f") == 0) {
                WriteRecordedTaskPath(L"");
                SafeAddLog("SYSTEM: OptiMax will no longer start with Windows.");
            }
            else {
                SafeAddLog("SYSTEM WARNING: Could not remove OptiMax from Windows startup.");
            }
        }
    }
}

// THE FIX: the logon task used to be re-registered on every single launch, from
// the config-loading loop, using whatever path the running copy happened to have.
// That turned any leftover older build into a self-perpetuating problem: whenever
// the old copy ran it re-pointed the logon task back at itself, so it started
// again at the next logon - and alongside whatever newer copy the user launched,
// which is exactly how two OptiMax instances ended up running at once and how a
// v3.8 build kept coming back from the dead.
//
// The task is now touched only when the recorded path no longer matches this
// executable. That also removes a schtasks.exe spawn from every startup, which is
// precisely the kind of repeated behaviour antivirus heuristics score against.
// THE FIX (round 2): the version above stopped the task being re-registered on
// EVERY launch, but the rule it left behind was still "whichever copy ran most
// recently owns the logon slot". That is not enough to stop a build directory
// resurrecting itself - it only slows it down. One F5 in Visual Studio re-points
// the task at out\build\, and the next logon starts that copy instead of the
// installed one. That is exactly what happened on 2026-07-28, hours after the
// original fix had been verified working: VS rebuilt into out\build\x64-release,
// that copy ran once, and it took the logon slot straight back.
//
// The rule now is a hierarchy rather than recency:
//   - a copy running from a build directory NEVER claims the logon slot;
//   - an installed copy claims it if nothing holds it, if the holder no longer
//     exists, or if the holder is a build directory that should not have had it.
// Two installed copies still fall back to last-one-wins, which is the right
// behaviour for a genuine move or reinstall.
void SyncStartupTask() {
    wchar_t szPath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, szPath, MAX_PATH)) return;
    std::wstring self(szPath);
    std::wstring recorded = ReadRecordedTaskPath();

    if (!g_settingAutoStart.load()) {
        // Auto-start is off but a task is still registered from a previous
        // install. Remove it, or it keeps launching a build the user has
        // already decided they do not want starting with Windows.
        if (!recorded.empty()) SetStartupRegistry(false);
        return;
    }

    // Windows paths are case-insensitive. The old exact comparison re-registered
    // the task - spawning schtasks.exe, the very thing this function exists to
    // avoid doing repeatedly - whenever nothing differed but the casing.
    if (!recorded.empty() && _wcsicmp(recorded.c_str(), self.c_str()) == 0) return;

    if (IsBuildOutputCopy(self)) {
        SafeAddLog(PathExistsW(recorded)
            ? "SYSTEM: Running from a build directory - leaving the startup entry pointing at the installed copy."
            : "SYSTEM: Running from a build directory - not registering it to start with Windows.");
        return;
    }

    if (recorded.empty()) {
        SafeAddLog("SYSTEM: Registering this copy of OptiMax to start with Windows.");
    }
    else if (!PathExistsW(recorded)) {
        SafeAddLog("SYSTEM: The startup entry pointed at a copy of OptiMax that no longer exists - re-pointing it at this one.");
    }
    else if (IsBuildOutputCopy(recorded)) {
        SafeAddLog("SYSTEM: The startup entry pointed at a build directory - re-pointing it at this installed copy.");
    }
    else {
        SafeAddLog("SYSTEM: Startup entry points at a different copy of OptiMax - re-pointing it at this one.");
    }
    SetStartupRegistry(true);
}

// --- TRAY & WINDOW HANDLER ---

// THE FIX: this used to be inline only inside the SC_MINIMIZE case. Pulling it
// out means the new close-confirmation dialog's "Minimize to Tray" option can
// reuse the exact same behavior instead of duplicating it.
void MinimizeToTray(HWND hWnd) {
    if (!OptiMax::IsExplorerRunning()) {
        OptiMax::ToggleExplorer();
        SafeAddLog("SYSTEM: Explorer shell restarted so the tray icon is reachable.");
    }

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hWnd;
    g_nid.uID = 1001;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    wcscpy_s(g_nid.szTip, L"OptiMax Pro v4.2.0");

    // THE FIX: this result used to be discarded. Combined with the ToggleExplorer
    // race fix, this should already succeed - but as defense in depth, retry once
    // after a short wait, and critically: never hide the window if registration
    // never succeeds. A hidden window with no tray icon to bring it back is a
    // dead end for the user - better to leave the window visible than risk that.
    BOOL registered = Shell_NotifyIconW(NIM_ADD, &g_nid);
    if (!registered) {
        Sleep(500);
        registered = Shell_NotifyIconW(NIM_ADD, &g_nid);
    }

    if (registered) {
        ShowWindow(hWnd, SW_HIDE);
    }
    else {
        SafeAddLog("SYSTEM WARNING: Could not create the tray icon, so the window was minimized normally instead of hidden - otherwise there'd be no way to get it back.");
        ShowWindow(hWnd, SW_MINIMIZE);
    }
}

#define ID_TRAY_RESTORE 2001
#define ID_TRAY_EXIT 2002

// Sent by a second instance that has just found this one already running. A
// registered message rather than WM_USER+n: the value is negotiated with the
// system from the string, so it cannot collide with anything else the window
// receives, and both processes derive the same number without sharing a header.
static const wchar_t* kShowExistingMsgName = L"OptiMaxProShowExisting";

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    // Not a case label: RegisterWindowMessage resolves at runtime, so this
    // cannot live in the switch below.
    static const UINT s_showExisting = RegisterWindowMessageW(kShowExistingMsgName);
    if (msg == s_showExisting) {
        // Same sequence the tray icon's own restore uses. Needed because the
        // running instance may be hidden in the tray, and SetForegroundWindow
        // on a hidden window does nothing at all - which would leave someone
        // clicking the shortcut over and over with no visible response.
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        ShowWindow(hWnd, SW_RESTORE);

        // Windows only lets a process hand over the foreground if it holds it,
        // so this can legitimately fail depending on what had focus when the
        // second copy was started - observed failing in exactly that situation.
        // The window is restored and visible either way; if it could not be
        // raised, flash the taskbar button rather than leaving it to be found.
        if (!SetForegroundWindow(hWnd)) {
            FLASHWINFO fi = { sizeof(fi) };
            fi.hwnd = hWnd;
            fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
            fi.uCount = 3;
            fi.dwTimeout = 0;
            FlashWindowEx(&fi);
        }
        return 0;
    }

    switch (msg) {
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MINIMIZE) {
            MinimizeToTray(hWnd);
            return 0;
        }
        break;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid); ShowWindow(hWnd, SW_RESTORE); SetForegroundWindow(hWnd);
        }
        else if (lParam == WM_RBUTTONUP) {
            // THE FEATURE: a real context menu instead of right-click doing the same
            // thing as left-click. This is also now the only way to fully exit once
            // "always minimize to tray" is the remembered close behavior.
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTORE, L"Restore OptiMax");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit App");

            // Required so the menu closes properly if the user clicks away from it.
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
            PostMessage(hWnd, WM_NULL, 0, 0);
            DestroyMenu(hMenu);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_RESTORE) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid); ShowWindow(hWnd, SW_RESTORE); SetForegroundWindow(hWnd);
        }
        else if (LOWORD(wParam) == ID_TRAY_EXIT) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && g_pSwapChain != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget(); g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0); CreateRenderTarget();
        }
        return 0;
    case WM_CLOSE:
        // THE FIX: closing the launch screen exits, full stop - no tray, no
        // dialog. Two reasons. Nothing has started yet at that point, so there is
        // nothing for a background instance to keep doing; and the confirmation
        // dialog is only drawn by the main UI, so with the default "ask every
        // time" setting the X button on the sign-in screen set a flag that
        // nothing was listening for and the window simply refused to close. With
        // "always minimize to tray" it did something arguably worse: silently
        // parked an app the user had never signed into.
        if (OptiMax::Auth::IsLaunchScreenActive()) {
            DestroyWindow(hWnd);
            return 0;
        }

        // THE FEATURE: g_closeBehavior persists the user's choice from the dialog.
        // 0 = ask every time (default), 1 = always minimize to tray, 2 = always exit.
        // Once set to 1, the X button never shows the dialog again - the only way
        // to fully exit becomes the tray icon's right-click "Exit App".
        if (g_closeBehavior.load() == 1) {
            MinimizeToTray(hWnd);
        }
        else if (g_closeBehavior.load() == 2) {
            DestroyWindow(hWnd);
        }
        else {
            g_showCloseConfirm = true;
        }
        return 0;
    case WM_DESTROY: ::PostQuitMessage(0); return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ========================================================================
// WINDOWS ENTRY POINT
// ========================================================================
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow) {

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const bool relaunchedByUpdater = (wcsstr(pCmdLine, L"-updated") != nullptr);

    // Probe for an existing instance BEFORE elevating. The mutex below is the
    // authoritative guard, but it cannot run until after the elevation relaunch
    // (see there for why), and by that point a second launch has already put a
    // UAC prompt in front of someone who just wanted the window they already
    // had open. FindWindow is not subject to UIPI, so this works from a
    // non-elevated process; the message it posts is allowed through explicitly
    // where the window is created.
    //
    // Skipped for an update relaunch: its predecessor is still exiting and its
    // window is expected to still be there, so finding one is not a reason to
    // give up - that case is handled by waiting on the mutex instead.
    if (!relaunchedByUpdater) {
        HWND existing = FindWindowW(L"OptiMaxDX", nullptr);
        if (existing) {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(existing, RegisterWindowMessageW(kShowExistingMsgName), 0, 0);
            return 0;
        }
    }

    if (!OptiMax::IsElevated()) {
        wchar_t szPath[MAX_PATH];
        if (GetModuleFileNameW(NULL, szPath, ARRAYSIZE(szPath))) {
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.lpVerb = L"runas";
            sei.lpFile = szPath;
            // THE FIX: the command line was dropped on the way through elevation,
            // so a launch that had to elevate lost its arguments entirely - the
            // logon task's "-tray" among them, which is why an elevation round
            // trip could turn a silent tray start into a full window appearing
            // in the user's face at every boot.
            sei.lpParameters = pCmdLine;
            sei.hwnd = NULL;
            sei.nShow = SW_NORMAL;
            if (ShellExecuteExW(&sei)) return 0;
        }
    }

    // ---------------------------------------------------------------------
    // SINGLE INSTANCE
    //
    // Placed AFTER the elevation relaunch above, and that ordering is
    // load-bearing. The unelevated process launches the elevated one and then
    // returns; if it had already taken this mutex, the elevated child would
    // start, find the name taken, and exit - and OptiMax would never open at all.
    //
    // Two instances were reachable by several ordinary routes: the logon task
    // starts one with -tray while the user starts another from a shortcut; the
    // updater relaunches before the outgoing process has finished exiting; or a
    // UAC prompt is declined, which leaves the unelevated instance running
    // because the branch above deliberately falls through.
    // ---------------------------------------------------------------------
    // Only an update relaunch has a good reason to wait: it knows the process
    // holding the mutex is its own predecessor and is already exiting. Measured
    // on a real run, waiting unconditionally cost every ordinary second launch a
    // flat five seconds of nothing happening before the existing window came
    // back - which reads as a broken shortcut, not as a guard doing its job.
    //
    // This is still needed alongside the pre-elevation probe above: the probe
    // only fires once a window exists, so two copies started within the same
    // second - before either has created one - would both get past it.
    HANDLE hSingleInstance = CreateMutexW(nullptr, TRUE, L"Local\\OptiMaxPro_SingleInstance");
    if (hSingleInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        // WAIT_ABANDONED is the expected success here: it means the holder
        // exited without releasing, which is exactly what we were waiting for.
        DWORD waited = WaitForSingleObject(hSingleInstance, relaunchedByUpdater ? 5000 : 0);
        if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
            // A real second instance. Give the running copy the foreground
            // rather than vanishing silently - a user who clicks the icon and
            // sees nothing happen will simply keep clicking.
            HWND existing = FindWindowW(L"OptiMaxDX", nullptr);
            if (existing) {
                // Ask it to surface itself. It has to do the work: it may be
                // hidden in the tray, and only the owning process can undo that
                // cleanly (the tray icon has to come down with the window).
                AllowSetForegroundWindow(ASFW_ANY);
                PostMessageW(existing, RegisterWindowMessageW(kShowExistingMsgName), 0, 0);
            }
            CloseHandle(hSingleInstance);
            return 0;
        }
    }

    // Housekeeping for the in-place updater: it renames the previous binary to
    // <name>.old and starts the new one, so the first thing the new version does
    // is clear that away.
    {
        wchar_t selfPath[MAX_PATH];
        if (GetModuleFileNameW(NULL, selfPath, ARRAYSIZE(selfPath))) {
            std::wstring stale = std::wstring(selfPath) + L".old";
            if (!DeleteFileW(stale.c_str()) &&
                GetFileAttributesW(stale.c_str()) != INVALID_FILE_ATTRIBUTES) {
                // Still locked - antivirus mid-scan, or Explorer holding a
                // handle. Queue it for deletion at the next boot so a previous
                // version can never sit next to the current one indefinitely,
                // which is how people end up running the wrong binary.
                MoveFileExW(stale.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
            }
        }

        // The updater's staging file. Left behind whenever a download was
        // interrupted or failed verification, and it is a complete copy of an
        // OptiMax binary - frequently an older one - sitting in %TEMP%.
        wchar_t tempDir[MAX_PATH];
        if (GetTempPathW(ARRAYSIZE(tempDir), tempDir)) {
            std::wstring staged = std::wstring(tempDir) + L"OptiMax_UpdateTarget.exe";
            DeleteFileW(staged.c_str());
        }
    }

    // Records which copy is actually running. When several builds exist on a
    // machine, this line in the log is what identifies the one that started.
    {
        wchar_t selfPath[MAX_PATH];
        if (GetModuleFileNameW(NULL, selfPath, ARRAYSIZE(selfPath))) {
            std::wstring p(selfPath);
            SafeAddLog("SYSTEM: Running v4.2.0 from " + std::string(p.begin(), p.end()));
        }
        // Surfaced in the GUI so a user asked for "your log" can find the file
        // without being talked through where it lives.
        std::wstring logPath = GetLogFilePath();
        if (!logPath.empty()) {
            SafeAddLog("LOG: This session is being written to " + std::string(logPath.begin(), logPath.end()));
        }
        else {
            SafeAddLog("LOG WARNING: No log file could be opened - only the in-app log is available this session.");
        }
    }

    bool startToTray = false;
    if (wcsstr(pCmdLine, L"-tray") != nullptr) {
        startToTray = true;
    }

    bool hadExistingConfig = LoadPersistentState();
    if (!hadExistingConfig) {
        // THE FIX: this used to call SetStartupRegistry(true) outright, so a
        // first run silently installed a Run key before the user had agreed to
        // anything. That is the behaviour that separates "a tool" from
        // "potentially unwanted software" under most antivirus vendors' published
        // criteria, and it was a fair thing for them to flag - installing
        // persistence is the user's decision to make, not ours.
        //
        // The feature itself is unchanged: the prompt defaults to Yes, one click
        // sets exactly the same Run key, and the Settings toggle works as before.
        // The only difference is that the user is asked once.
        g_settingAutoStart = false;
        SafeAddLog("SYSTEM: First run detected.");
        SavePersistentState();
    }

    // Ask until it's actually been answered, rather than only on the very first
    // launch - quitting from the sign-in screen on a first run would otherwise
    // mean never being offered it at all. Skipped when it's already on, so
    // upgrades from the version that enabled it silently aren't asked to turn on
    // something that is already running.
    if (!g_autoStartAsked.load() && !g_settingAutoStart.load()) {
        g_askAutoStartOnFirstRun = true;
    }

    // Point the logon task at this executable if it is currently aimed somewhere
    // else, or remove it if auto-start has been turned off. No-op in the normal
    // case, so it costs nothing on a machine with only one copy installed.
    SyncStartupTask();

    // --- TRUE BOOT-TIME EXECUTION FOR PERSISTENT STATES ---
    // THE FIX: both re-apply calls below hardcoded `false` for their safe-mode
    // argument. Everywhere else that argument carries "is an anti-cheat running"
    // (the UI passes g_isAntiCheatActive), and it exists to keep OptiMax off the
    // undocumented Nt* APIs while one is watching. Since OptiMax is normally
    // started by a logon task, and anti-cheat services start at boot too, the one
    // path most likely to run alongside a freshly-started anti-cheat was the only
    // one that never checked. Ask the same question the UI asks.
    const bool antiCheatAtBoot =
        (g_isCoreActive.load() || g_isolationActive.load()) ? OptiMax::IsAntiCheatRunning() : false;

    // THE FIX: ordering. The TCP Latency Tweaks toggle and Core Tweaks both own
    // NetworkThrottlingIndex and SystemResponsiveness. OptimizeNetworkLatency(true)
    // now snapshots those two values so RevertOptimizations() can put back what was
    // really there - but it used to run BEFORE the persistent TCP tweak was applied,
    // so on every launch it snapshotted the pre-TCP values and the first game exit
    // reverted the TCP tweak's half of them. Applying the persistent TCP setting
    // first means the snapshot captures the state the user actually asked for.
    // Verified against this machine: NetworkTweaked was set to 1 in the registry
    // while both values sat at the Windows defaults, which is that loss already
    // having happened.
    if (g_networkTweaked.load()) {
        OptiMax::V3::BypassNagleAlgorithm();
    }

    if (g_isCoreActive.load()) {
        OptiMax::SetTimer(true, antiCheatAtBoot);
        OptiMax::SetGPUPriorityHigh();
        OptiMax::Power::EngageUltimatePower();
        OptiMax::AdvancedPower::DisableCoreParking();
        OptiMax::AdvancedPower::SetEPPToPerformance();
        OptiMax::OptimizeNetworkLatency(true);
    }

    if (g_isolationActive.load()) {
        OptiMax::EnableExtremeMode(antiCheatAtBoot);
    }

    if (g_coresUnparked.load()) {
        OptiMax::AdvancedPower::DisableCoreParking();
        OptiMax::AdvancedPower::SetEPPToPerformance();
    }

    if (g_servicesSanitized.load()) {
        OptiMax::Debloat::DisableTelemetry();
        OptiMax::Debloat::StopBloatServices();
    }

    // Experimental module boot execution
    if (g_msiEnabled.load()) {
        OptiMax::V3::InjectMSIMode();
    }
    if (g_vbsDisabled.load()) {
        OptiMax::V3::DisableVBS();
    }
    if (g_irqPinned.load()) {
        OptiMax::V3::PinGpuIrq();
    }
    // ------------------------------------------------------

    HRESULT hres = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    HICON hAppIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, hAppIcon, nullptr, nullptr, nullptr, L"OptiMaxDX", hAppIcon };
    ::RegisterClassExW(&wc);

    // ACCOUNTS: the launch (sign-in) screen is a compact window; the main UI is
    // landscape. Deciding here rather than after the window exists avoids a
    // visible resize flash on startup.
    //
    // HasStoredSession() is deliberately local-only (registry read + DPAPI
    // decrypt, no network), so this costs microseconds. If a remembered session
    // exists we go straight to the landscape size and show a brief "restoring"
    // state; only if that exchange later fails does the window shrink.
    bool startSignedIn = OptiMax::Auth::HasStoredSession();

    RECT startWork = { 0, 0, 1920, 1080 };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &startWork, 0);

    // The process is already per-monitor DPI aware by this point, so ask for the
    // real scale rather than assuming 100% - at 150% the window used to come up
    // sized for a display that wasn't there.
    float startDpi = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromPoint(POINT{ startWork.left, startWork.top }, MONITOR_DEFAULTTOPRIMARY));
    if (startDpi <= 0.0f) startDpi = 1.0f;

    int startW = (int)(1380 * startDpi), startH = (int)(900 * startDpi);
    if (!startSignedIn) {
        GetLaunchClientSize(startDpi, startWork, startW, startH);
    }
    else {
        int maxW = (startWork.right - startWork.left) - (int)(48 * startDpi);
        int maxH = (startWork.bottom - startWork.top) - (int)(48 * startDpi);
        if (maxW > 0 && startW > maxW) startW = maxW;
        if (maxH > 0 && startH > maxH) startH = maxH;
    }

    // Size the CLIENT area, so the ImGui content itself is 9:16 rather than
    // 9:16 minus the title bar and borders.
    RECT desired = { 0, 0, startW, startH };
    AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);
    int frameW = desired.right - desired.left;
    int frameH = desired.bottom - desired.top;

    RECT workArea = { 0, 0, 1920, 1080 };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int startX = workArea.left + ((workArea.right - workArea.left) - frameW) / 2;
    int startY = workArea.top + ((workArea.bottom - workArea.top) - frameH) / 2;
    if (startY < workArea.top) startY = workArea.top;

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"OptiMax Pro-Tuner", WS_OVERLAPPEDWINDOW,
        startX, startY, frameW, frameH, nullptr, nullptr, wc.hInstance, nullptr);

    if (hwnd) {
        // OptiMax runs elevated. User Interface Privilege Isolation blocks
        // messages sent from a lower integrity level to a higher one, silently,
        // so a second launch that has NOT yet elevated cannot ask this window to
        // surface itself - the post simply disappears and the user sees nothing.
        // Allowing this one registered message through is what makes the
        // pre-elevation probe in wWinMain work, and it opens nothing else: the
        // message carries no parameters and only ever restores the window.
        ChangeWindowMessageFilterEx(hwnd, RegisterWindowMessageW(kShowExistingMsgName),
            MSGFLT_ALLOW, nullptr);

        BOOL useDarkMode = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
        COLORREF titleBarColor = RGB(5, 5, 8);
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &titleBarColor, sizeof(titleBarColor));
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &titleBarColor, sizeof(titleBarColor));

        if (CreateDeviceD3D(hwnd)) {

            if (startToTray) {
                g_nid.cbSize = sizeof(NOTIFYICONDATAW);
                g_nid.hWnd = hwnd;
                g_nid.uID = 1001;
                g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
                g_nid.uCallbackMessage = WM_TRAYICON;
                g_nid.hIcon = hAppIcon;
                wcscpy_s(g_nid.szTip, L"OptiMax Pro v4.2.0");
                Shell_NotifyIconW(NIM_ADD, &g_nid);
                ShowWindow(hwnd, SW_HIDE);
            }
            else {
                ::ShowWindow(hwnd, nCmdShow);
            }

            ::UpdateWindow(hwnd);
            OptiMaxApp app(hwnd, hInstance, nCmdShow, startToTray);
            app.Run();
        }
        else {
            // THE FIX: if Direct3D 11 could not be initialised, the process fell
            // straight through to cleanup and exited with code 0 - no window, no
            // message, nothing in the log. From the user's side, double-clicking
            // OptiMax simply did nothing at all, forever, with no way to find out
            // why. This is the one failure that has to be said out loud, because
            // there is no UI left to say it in.
            MessageBoxW(nullptr,
                L"OptiMax could not start Direct3D 11.\n\n"
                L"This usually means the graphics driver needs updating, or the app is running "
                L"in a session without a GPU available (such as a remote desktop connection).",
                L"OptiMax Pro", MB_OK | MB_ICONERROR);
        }
        CleanupDeviceD3D();
    }
    else {
        MessageBoxW(nullptr, L"OptiMax could not create its main window.", L"OptiMax Pro", MB_OK | MB_ICONERROR);
    }

    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (SUCCEEDED(hres)) CoUninitialize();
    return 0;
}

// --- DIRECTX FOUNDATION ---
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    if (!g_pSwapChain || !g_pd3dDevice) return;
    ID3D11Texture2D* pBB = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBB))) && pBB != nullptr) {
        g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRenderTargetView);
        pBB->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}