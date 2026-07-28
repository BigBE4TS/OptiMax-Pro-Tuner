#pragma once

// --- CRITICAL FIX FOR 180 NETWORK ERRORS ---
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
// -------------------------------------------

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <d3d11.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <algorithm>
#include <sstream>
#include <map>
#include <tlhelp32.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Core Includes
#include "OptiMaxGlobals.h" 
#include "OptiMaxEngine.h"
#include "OptiMaxProfiler.h"
#include "OptiMaxAutoBoost.h"
#include "OptiMaxDebloat.h"
#include "OptiMaxDiscord.h"
#include "OptiMaxUWP.h"
#include "OptiMaxPower.h"
#include "OptiMaxBenchmark.h" 
#include "OptiMaxHardware.h"
#include "OptiMaxVerifier.h"

// UI & Tweaks Includes
#include "OptiMaxUi_Widgets.h"
#include "OptiMaxExperimental.h"
#include "OptiMaxFileDialog.h"
#include "OptiMaxUpdater.h"
#include "OptiMaxCloudSync.h"
#include "OptiMaxJson.h"
#include "OptiMaxAuth.h"
#include "OptiMaxImage.h"

#define WM_TRAYICON (WM_USER + 1)

// --- EXTERN DECLARATIONS FOR MAIN.CPP ---
extern std::atomic<bool> g_isAppRunning;
extern std::atomic<bool> g_showCloseConfirm;
extern std::atomic<int> g_closeBehavior;
extern void MinimizeToTray(HWND hWnd);
extern std::atomic<bool> g_isAntiCheatActive;
extern std::atomic<bool> g_isGameRunning;
extern std::atomic<bool> g_isCoreActive;
extern std::atomic<bool> g_autoBoostEnabled;

extern std::atomic<bool> g_coresUnparked;
extern std::atomic<bool> g_bootOptimized;
extern std::atomic<bool> g_servicesSanitized;
extern std::atomic<bool> g_privacyTweaked;
extern std::atomic<bool> g_extendedServices;
extern std::atomic<bool> g_telemetryTasks;
extern std::atomic<bool> g_snappyUI;
extern std::atomic<bool> g_mouseAccelDisabled;
extern std::atomic<bool> g_gameDvrDisabled;
extern std::atomic<bool> g_mmcssGames;
extern std::atomic<bool> g_globalTimerRes;
extern std::atomic<bool> g_isolationActive;
extern std::atomic<bool> g_pCoresLocked;
extern std::map<std::string, bool> g_gamePCoreOverrides;
extern std::map<std::string, bool> g_gameDisableSMT;
extern std::map<std::string, bool> g_gameHighPriority;
extern std::map<std::string, bool> g_gameAggressiveRAMPurge;
extern std::map<std::string, bool> g_gameDisableFSO;

extern std::atomic<bool> g_msiEnabled;
extern std::atomic<bool> g_networkTweaked;
extern std::atomic<bool> g_fseForced;
extern std::atomic<bool> g_vbsDisabled;
extern std::atomic<bool> g_irqPinned;

extern std::atomic<bool> g_settingAutoStart;
// Set by wWinMain when the UI should ask about "Start with Windows", replacing
// the old behaviour of installing the Run key without asking. g_autoStartAsked
// is the persisted record that the question has been put, so declining sticks.
extern std::atomic<bool> g_askAutoStartOnFirstRun;
extern std::atomic<bool> g_autoStartAsked;
extern std::atomic<bool> g_settingSafeMode;
extern std::atomic<bool> g_settingMpoDisable;
extern std::atomic<bool> g_settingAutoRam;
extern std::atomic<int> g_ramCleanThreshold;

extern std::mutex g_configMutex;

// THE FEATURE: resolves whether P-cores should be locked for a specific game,
// checking its per-game override first and falling back to the global default.
// exeName is expected as a wide string (matches activeGameName's type at the
// call site); converted narrow for the override map lookup.
// THE FIX: this used to sit above g_configMutex's own extern declaration and
// failed to compile as a result - moved below everything it depends on.
inline bool ShouldLockPCoresForGame(const std::wstring& exeName) {
    std::string narrowName(exeName.begin(), exeName.end());
    std::lock_guard<std::mutex> lock(g_configMutex);
    auto it = g_gamePCoreOverrides.find(narrowName);
    if (it != g_gamePCoreOverrides.end()) return it->second;
    return g_pCoresLocked.load();
}

// THE FEATURE: resolves one per-game flag out of any of the four flag maps.
// A game with no entry in that map reads as false, so every game that predates
// these flags keeps behaving exactly as it always did.
// Same wide->narrow conversion and same g_configMutex guard as
// ShouldLockPCoresForGame above - and, for the same reason, it has to sit BELOW
// g_configMutex's extern declaration. Putting a helper like this above the
// extern it depends on is what broke the build twice already.
inline bool GetGameFlag(const std::map<std::string, bool>& flagMap, const std::wstring& exeName) {
    if (exeName.empty()) return false;
    std::string narrowName(exeName.begin(), exeName.end());
    std::lock_guard<std::mutex> lock(g_configMutex);
    auto it = flagMap.find(narrowName);
    return (it != flagMap.end()) ? it->second : false;
}

extern std::vector<std::string> g_targetGames;
extern std::atomic<bool> g_isBenchmarking;
extern std::mutex g_benchMutex;
extern OptiMax::Benchmark::AuditResult g_lastAudit;
extern ULONGLONG g_benchStartTime;
extern std::thread g_benchThread;

extern std::mutex g_logMutex;
extern std::vector<std::string> g_logs;
extern void SavePersistentState();

extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;
extern IDXGISwapChain* g_pSwapChain;
extern ID3D11RenderTargetView* g_mainRenderTargetView;
extern NOTIFYICONDATAW g_nid;

extern void SafeAddLog(const std::string& msg);
extern bool ExportProfile(const std::wstring& path);
extern bool ImportProfile(const std::wstring& path);
extern void SetStartupRegistry(bool enable);
extern void CleanupDeviceD3D();

// --- BACKGROUND THROTTLING REVERTER ---
// Puts every process EnableExtremeMode() lowered back to its defaults. (Named for
// what it does - nothing here goes anywhere near kernel code.)
//
// THE FIX (1/2): this walked EVERY process and forced NORMAL_PRIORITY_CLASS,
// including the ones EnableExtremeMode() deliberately never touched. Several of
// those legitimately run above Normal - audiodg.exe is the audio engine and gets
// demoted into glitching, csrss/wininit run High by design - and it also stomped
// the game's own High priority that the per-game "high priority" flag had just
// set. Reverting a tweak must not reach past what the tweak touched, so this now
// honours the same protected whitelist.
//
// THE FIX (2/2): EnableExtremeMode() lowers three things - CPU priority class,
// disk I/O priority, and memory page priority - and this restored only the first.
// The other two stayed pinned at Very Low for the remaining life of every
// background process, so "Turn Off All Tweaks" left downloads, backups and file
// transfers still crawling while the log said everything was off.
inline void RevertIsolationPriorities() {
    OptiMax::ScopedHandle hSnap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                std::wstring exeName = pe.szExeFile;
                if (OptiMax::IsProtectedProcess(exeName)) continue;

                OptiMax::ScopedHandle hP(OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID));
                if (hP) {
                    SetPriorityClass(hP, NORMAL_PRIORITY_CLASS);
                    ULONG ioPrio = 2;  // IoPriorityNormal - the default for a normal process
                    NtSetInformationProcess(hP, 33, &ioPrio, sizeof(ioPrio));
                    ULONG memPrio = 5; // MEMORY_PRIORITY_NORMAL - the default
                    NtSetInformationProcess(hP, 39, &memPrio, sizeof(memPrio));
                }
            } while (Process32NextW(hSnap, &pe));
        }
    }
}

// --- LAUNCH (SIGN-IN) WINDOW SIZE ---
// Two places need this answer: wWinMain, which has to size the window before it
// exists so startup doesn't visibly resize itself, and ApplyWindowLayout, which
// resizes when the user signs out and comes back. They used to compute it
// separately and had already drifted, so it lives in one function.
//
// The screen this sizes is a single short column of fields. The old layout gave
// it a full-height 9:16 portrait window, which on a 1080p desktop meant a ~990px
// tall window holding maybe 600px of form - it read as a phone app that had
// wandered onto a PC. This is the CLIENT size, so the ImGui content gets these
// dimensions rather than these minus the title bar.
inline void GetLaunchClientSize(float dpi, const RECT& workArea, int& outW, int& outH) {
    if (dpi <= 0.0f) dpi = 1.0f;

    // Fits the create-account form (the taller of the two modes - four fields
    // rather than two) with room for a wrapped error line, and nothing beyond
    // that. Sign-in mode is ~140px shorter and simply sits higher in the window.
    int w = (int)(470.0f * dpi);
    int h = (int)(720.0f * dpi);

    int maxW = (workArea.right - workArea.left) - (int)(48.0f * dpi);
    int maxH = (workArea.bottom - workArea.top) - (int)(48.0f * dpi);
    if (maxW > 0 && w > maxW) w = maxW;
    if (maxH > 0 && h > maxH) h = maxH;

    // Below this the form genuinely doesn't fit and clipping would be worse than
    // a window that slightly overhangs a very short work area.
    if (w < 380) w = 380;
    if (h < 520) h = 520;

    outW = w;
    outH = h;
}

// --- THE CORE APPLICATION CLASS ---
class OptiMaxApp {
private:
    HWND hwnd;
    HINSTANCE hInstance;
    std::thread telemetryWorker;

    ImFont* m_fontRegular = nullptr;
    ImFont* m_fontBold = nullptr;

    bool done = false;
    int activeTab = 0;

    std::string hwCPU, hwRAM, hwGPU, hwOS;
    bool isHeterogeneousCPU;
    bool isWindows11 = false;   // set by the OS-detection module below; gates Win11-only tweaks
    bool isAdmin;
    bool isD3DReady;

    // UI Splash Logic ("startup phase")
    bool showSplash;        // currently on screen
    bool splashPending;     // armed, but waiting for the launch screen to finish
    ULONGLONG splashStartTime;

    // Updater Logic
    //
    // CURRENT_VERSION is the single source of truth for the app's version.
    // release.ps1 reads it straight out of this line to derive the git tag and
    // the contents of version.txt, so bumping it here is the only edit a
    // release needs - the three published artefacts can no longer drift apart
    // from the binary or from each other.
    const std::string CURRENT_VERSION = "4.2.0";

    // THE FIX: these were plain members, written on the telemetry worker thread
    // and read on the UI thread every single frame with nothing between them.
    // A concurrent read of a std::string mid-assignment is undefined behaviour,
    // not merely a stale value - the renderer could be handed a pointer the
    // worker is in the middle of reallocating. The mutex covers the string; the
    // flag is atomic so the popup trigger can be tested and cleared each frame
    // without taking a lock.
    std::mutex updateInfoMutex;
    std::string latestVersion = "";
    std::atomic<bool> showUpdatePrompt{ false };

    std::string GetLatestVersionSafe() {
        std::lock_guard<std::mutex> lock(updateInfoMutex);
        return latestVersion;
    }

    // THE FIX: "Create a System Restore Point Before Applying Tweaks" was honoured
    // in exactly one place - Core Tweaks - even though its wording promises to cover
    // all of them. The operations a user would most want a way back from had no
    // restore point at all: boot configuration, VBS, MSI mode and GPU interrupt
    // affinity all change how Windows starts, and a bad interrupt or boot setting is
    // precisely the failure that leaves someone unable to get back in to undo it.
    // The safety net covered the one tweak that needed it least.
    //
    // Windows throttles restore point creation (once per 24h by default), so routing
    // every destructive action through here does not fill the disk - subsequent calls
    // inside that window are declined by the OS, which CreateRestorePoint logs.
    void RestorePointBeforeTweak() {
        if (g_settingSafeMode.load()) OptiMax::CreateRestorePoint();
    }
    // isDownloadingUpdate moved to the global g_isDownloadingUpdate (OptiMaxUpdater.h) -
    // the updater itself needs to be able to reset it on failure, which a per-instance
    // class member here couldn't support.

    ULONGLONG lastSmartPurgeTime = 0;
    ULONGLONG lastFullPurgeTime = 0;
    bool timerResolutionVerifiedThisSession = false;
    // Debounce for the zero-config fullscreen game detector: a real game holds
    // fullscreen for many seconds, whereas transient fullscreen overlays (the
    // Win+Shift+S screen-snip, alt-tab, a briefly-fullscreen launcher) appear for
    // a second or two. Requiring the SAME fullscreen exe for several consecutive
    // 1s checks before engaging Auto-Boost stops those overlays from triggering
    // the engage -> RAM purge -> revert thrash that caused the lag.
    std::wstring fsCandidate;
    int fsStableCount = 0;
    static const int kFullscreenStableTicks = 3;   // ~3 seconds
    bool isProfilerRunning = false;
    ULONGLONG lastProfileUpdate = 0;

    double cachedTimer = 0.0, cachedDpcSpike = 0.0;
    bool cachedMpo = false;
    int cachedRam = 0, cachedCpuLoad = 0, cachedGpuLoad = 0;

    PDH_HQUERY sysQuery = NULL;
    PDH_HCOUNTER cpuTotal = NULL, gpuTotal = NULL;

    float dpiScale, cardHeight, toggleAnchorY, bottomAnchorY;

    // --- ACCOUNTS / LAUNCH SCREEN ---
    // Field buffers are fixed char arrays because that's what ImGui::InputText
    // takes. Sized generously; Supabase caps none of these low enough to matter.
    bool  authCreateMode = false;          // false = sign in, true = create account
    char  authEmail[192] = { 0 };
    char  authPassword[128] = { 0 };
    char  authConfirm[128] = { 0 };
    char  authUsername[64] = { 0 };
    bool  authRemember = true;
    bool  windowIsPortrait = true;         // tracks which layout the window is in
    std::string authLocalError;            // client-side validation, distinct from server errors

    // Edit Profile modal
    bool  openEditProfile = false;
    char  editUsername[64] = { 0 };

    OptiMax::Image::Texture avatarTexture;

    // Switches between the compact launch (sign-in) window and the landscape
    // window the startup phase and main UI are designed for. The client area is
    // what's sized, not the outer frame, so the ImGui content gets the requested
    // dimensions rather than those minus the title bar.
    void ApplyWindowLayout(bool portrait) {
        int clientW, clientH;

        if (portrait) {
            RECT work = { 0, 0, 1920, 1080 };
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoW(mon, &mi)) work = mi.rcWork;

            GetLaunchClientSize(dpiScale, work, clientW, clientH);
        }
        else {
            clientW = (int)(1380 * dpiScale);
            clientH = (int)(900 * dpiScale);

            RECT work = { 0, 0, 1920, 1080 };
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoW(mon, &mi)) work = mi.rcWork;

            int maxW = (work.right - work.left) - (int)(48 * dpiScale);
            int maxH = (work.bottom - work.top) - (int)(48 * dpiScale);
            if (clientW > maxW) clientW = maxW;
            if (clientH > maxH) clientH = maxH;
        }

        RECT frame = { 0, 0, clientW, clientH };
        DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
        DWORD exStyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        AdjustWindowRectEx(&frame, style, FALSE, exStyle);

        int frameW = frame.right - frame.left;
        int frameH = frame.bottom - frame.top;

        RECT work2 = { 0, 0, 1920, 1080 };
        HMONITOR mon2 = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi2 = { sizeof(mi2) };
        if (GetMonitorInfoW(mon2, &mi2)) work2 = mi2.rcWork;

        int x = work2.left + ((work2.right - work2.left) - frameW) / 2;
        int y = work2.top + ((work2.bottom - work2.top) - frameH) / 2;
        if (y < work2.top) y = work2.top;

        SetWindowPos(hwnd, nullptr, x, y, frameW, frameH, SWP_NOZORDER | SWP_NOACTIVATE);
        windowIsPortrait = portrait;
    }

    // Draws the "OPTIMAX PRO" wordmark exactly as the loading screen does it:
    // same bold font, same pulse period, same 4px dark drop shadow, same purple.
    // Factored out so the splash and the launch screen can never drift apart.
    void DrawWordmark(float centerX, float topY, ULONGLONG now, float alpha,
        const ImVec4& purple) {
        if (m_fontBold) ImGui::PushFont(m_fontBold);

        const char* titleText = "OPTIMAX PRO";
        float titleWidth = ImGui::CalcTextSize(titleText).x;

        float pulse = (sinf(now / 150.0f) * 0.5f) + 0.5f;
        ImVec4 pulsePurple = ImVec4(purple.x, purple.y, purple.z, alpha * (0.7f + (0.3f * pulse)));

        ImGui::SetCursorPos(ImVec2(centerX - (titleWidth * 0.5f) + (4.0f * dpiScale), topY + (4.0f * dpiScale)));
        ImGui::TextColored(ImVec4(0.05f, 0.02f, 0.1f, alpha * 0.9f), titleText);

        ImGui::SetCursorPos(ImVec2(centerX - (titleWidth * 0.5f), topY));
        ImGui::TextColored(pulsePurple, titleText);

        if (m_fontBold) ImGui::PopFont();
    }

    // UVs that centre-crop a possibly non-square picture into a square frame.
    // Drawing 0,0-1,1 instead stretches the image to fit, so a 16:9 photo gets
    // squashed into the circle - the other half of "my profile picture looks
    // wrong". Done in UVs, so it costs nothing and the stored image is untouched.
    static void AvatarCropUV(const OptiMax::Image::Texture& tex, ImVec2& uv0, ImVec2& uv1) {
        uv0 = ImVec2(0.0f, 0.0f);
        uv1 = ImVec2(1.0f, 1.0f);
        if (tex.width <= 0 || tex.height <= 0) return;

        if (tex.width > tex.height) {
            float f = (float)tex.height / (float)tex.width;
            uv0.x = (1.0f - f) * 0.5f;
            uv1.x = uv0.x + f;
        }
        else if (tex.height > tex.width) {
            float f = (float)tex.width / (float)tex.height;
            uv0.y = (1.0f - f) * 0.5f;
            uv1.y = uv0.y + f;
        }
    }

    // The launch screen holds the app only while it actually has something to ask
    // the user. Defined in OptiMaxAuth.h because WndProc needs the same answer -
    // see IsLaunchScreenActive() there for why Restoring is excluded.
    bool AuthFormActive() const { return OptiMax::Auth::IsLaunchScreenActive(); }

    // Client-side checks so obvious mistakes don't cost a network round trip and
    // an unhelpful server error. Returns "" when the form is good to submit.
    std::string ValidateAuthForm() const {
        std::string email(authEmail);
        std::string pass(authPassword);

        if (email.empty()) return "Enter your email address.";
        size_t at = email.find('@');
        if (at == std::string::npos || at == 0 || email.find('.', at) == std::string::npos
            || email.back() == '.') {
            return "That doesn't look like a valid email address.";
        }
        if (pass.empty()) return "Enter your password.";

        if (authCreateMode) {
            // Supabase's default minimum is 6; check it here so the user finds out
            // before a round trip rather than after.
            if (pass.size() < 6) return "Use a password of at least 6 characters.";
            if (pass != std::string(authConfirm)) return "Those passwords don't match.";

            std::string user(authUsername);
            if (user.size() < 3) return "Pick a username of at least 3 characters.";
            if (user.size() > 24) return "Usernames can be at most 24 characters.";
            for (unsigned char c : user) {
                if (!isalnum(c) && c != '_' && c != '-') {
                    return "Usernames can only use letters, numbers, hyphens and underscores.";
                }
            }
        }
        return "";
    }

    void RenderLaunchScreen(ULONGLONG now, const ImVec4& purple, float btnH) {
        namespace Auth = OptiMax::Auth;

        // A signup that needs email confirmation leaves no session, so the next
        // thing the user has to do is sign in. Put them on that form rather than
        // leaving them staring at the create-account fields they just submitted.
        if (Auth::g_signupNeedsConfirmation.exchange(false)) {
            authCreateMode = false;
            authLocalError.clear();
            SecureZeroMemory(authPassword, sizeof(authPassword));
            SecureZeroMemory(authConfirm, sizeof(authConfirm));
        }

        bool busy = Auth::IsBusy();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("LaunchScreen", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // The main UI's 12px row spacing is right for cards with room to breathe;
        // on a compact sign-in form it's what forced a phone-shaped window. Tighter
        // spacing here is what lets the whole form fit in a short window.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
            ImVec2(ImGui::GetStyle().ItemSpacing.x, 5.0f * dpiScale));

        ImVec2 win = ImGui::GetWindowSize();

        // --- LOGO: same wordmark as the loading screen, centred at the top ---
        // Anchored to a fixed offset rather than a percentage of the height: the
        // form below is a fixed height, so a proportional gap just added dead space
        // at the top and pushed the window taller for no benefit.
        float wordmarkY = 18.0f * dpiScale;
        DrawWordmark(win.x * 0.5f, wordmarkY, now, 1.0f, purple);

        float y = wordmarkY + (32.0f * dpiScale);
        const char* tagline = "Pro-Tuner";
        float tagW = ImGui::CalcTextSize(tagline).x;
        ImGui::SetCursorPos(ImVec2((win.x - tagW) * 0.5f, y));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "%s", tagline);

        // --- Centred column, sized for the narrow portrait window ---
        float colW = win.x - (56.0f * dpiScale);
        if (colW > 400.0f * dpiScale) colW = 400.0f * dpiScale;
        float colX = (win.x - colW) * 0.5f;

        y += 34.0f * dpiScale;
        ImGui::SetCursorPos(ImVec2(colX, y));

        ImGui::BeginGroup();

        // Scoped rather than flattened: this used to be the "else" of a branch
        // that also rendered a "Restoring your session..." state. The startup
        // phase covers that state now (see AuthFormActive), so the only thing
        // this screen ever draws is the form.
        {
            // --- Mode switch ---
            if (m_fontBold) ImGui::PushFont(m_fontBold);
            const char* heading = authCreateMode ? "Create Account" : "Sign In";
            float headW = ImGui::CalcTextSize(heading).x;
            ImGui::SetCursorPosX(colX + (colW - headW) * 0.5f);
            ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "%s", heading);
            if (m_fontBold) ImGui::PopFont();

            ImGui::Dummy(ImVec2(colW, 8.0f * dpiScale));

            if (busy) ImGui::BeginDisabled();

            // --- Fields ---
            ImGui::SetCursorPosX(colX);
            ImGui::TextDisabled("Email");
            ImGui::SetCursorPosX(colX);
            ImGui::SetNextItemWidth(colW);
            ImGui::InputTextWithHint("##authEmail", "you@example.com", authEmail, sizeof(authEmail));

            if (authCreateMode) {
                ImGui::Dummy(ImVec2(colW, 2.0f * dpiScale));
                ImGui::SetCursorPosX(colX);
                ImGui::TextDisabled("Username");
                ImGui::SetCursorPosX(colX);
                ImGui::SetNextItemWidth(colW);
                ImGui::InputTextWithHint("##authUsername", "shown to other users", authUsername, sizeof(authUsername));
            }

            ImGui::Dummy(ImVec2(colW, 2.0f * dpiScale));
            ImGui::SetCursorPosX(colX);
            ImGui::TextDisabled("Password");
            ImGui::SetCursorPosX(colX);
            ImGui::SetNextItemWidth(colW);
            ImGui::InputText("##authPassword", authPassword, sizeof(authPassword),
                ImGuiInputTextFlags_Password);

            if (authCreateMode) {
                ImGui::Dummy(ImVec2(colW, 2.0f * dpiScale));
                ImGui::SetCursorPosX(colX);
                ImGui::TextDisabled("Confirm password");
                ImGui::SetCursorPosX(colX);
                ImGui::SetNextItemWidth(colW);
                ImGui::InputText("##authConfirm", authConfirm, sizeof(authConfirm),
                    ImGuiInputTextFlags_Password);
            }

            ImGui::Dummy(ImVec2(colW, 4.0f * dpiScale));
            ImGui::SetCursorPosX(colX);
            bool rememberTemp = authRemember;
            // The toggle's own label says it; the second explanatory line under it
            // was a whole row of height spent restating that.
            if (PremiumToggle("Remember me on this PC", &rememberTemp)) authRemember = rememberTemp;

            if (busy) ImGui::EndDisabled();

            // --- Messages ---
            // Two channels, deliberately. Red is reserved for "this didn't work".
            // "Account created, go check your inbox" is the flow succeeding, and
            // colouring it like a failure made people think they'd broken something.
            std::string shownError = authLocalError.empty() ? Auth::GetLastError() : authLocalError;
            std::string shownNotice = shownError.empty() ? Auth::GetLastNotice() : std::string();

            if (!shownError.empty() || !shownNotice.empty()) {
                ImGui::Dummy(ImVec2(colW, 4.0f * dpiScale));
                ImGui::SetCursorPosX(colX);
                ImGui::PushTextWrapPos(colX + colW);
                if (!shownError.empty()) {
                    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", shownError.c_str());
                }
                else {
                    ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.68f, 1.0f), "%s", shownNotice.c_str());
                }
                ImGui::PopTextWrapPos();
            }

            ImGui::Dummy(ImVec2(colW, 6.0f * dpiScale));

            // --- Primary action ---
            ImGui::SetCursorPosX(colX);
            if (busy) ImGui::BeginDisabled();
            const char* primaryLabel = busy
                ? (authCreateMode ? "Creating account..." : "Signing in...")
                : (authCreateMode ? "Create Account" : "Sign In");

            if (PremiumButton(primaryLabel, ImVec2(colW, btnH * 1.35f))) {
                authLocalError = ValidateAuthForm();
                if (authLocalError.empty()) {
                    Auth::ClearMessages();
                    if (authCreateMode) {
                        Auth::SignUp(authEmail, authPassword, authUsername, authRemember);
                    }
                    else {
                        Auth::SignIn(authEmail, authPassword, authRemember);
                    }
                }
            }
            if (busy) ImGui::EndDisabled();

            ImGui::Dummy(ImVec2(colW, 4.0f * dpiScale));

            // --- Mode toggle ---
            ImGui::SetCursorPosX(colX);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.10f));
            ImGui::PushStyleColor(ImGuiCol_Text, purple);
            if (ImGui::Button(authCreateMode
                ? "Already have an account? Sign in"
                : "Need an account? Create one", ImVec2(colW, btnH))) {
                authCreateMode = !authCreateMode;
                authLocalError.clear();
                Auth::ClearMessages();
                // Don't carry a typed password across a mode switch.
                SecureZeroMemory(authPassword, sizeof(authPassword));
                SecureZeroMemory(authConfirm, sizeof(authConfirm));
            }
            ImGui::PopStyleColor(4);

            // --- Guest path (this was an explicit product decision, not a default) ---
            ImGui::Dummy(ImVec2(colW, 2.0f * dpiScale));
            ImGui::SetCursorPosX(colX);
            ImGui::Separator();
            ImGui::Dummy(ImVec2(colW, 2.0f * dpiScale));

            ImGui::SetCursorPosX(colX);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.66f, 1.0f));
            if (ImGui::Button("Continue without an account", ImVec2(colW, btnH))) {
                Auth::ContinueAsGuest();
            }
            ImGui::PopStyleColor(4);

            const char* guestNote = "Every tweak works without an account.";
            float noteW = ImGui::CalcTextSize(guestNote).x;
            ImGui::SetCursorPosX(colX + (colW - noteW) * 0.5f);
            ImGui::TextColored(ImVec4(0.42f, 0.42f, 0.48f, 1.0f), "%s", guestNote);
        }

        ImGui::EndGroup();

        // Version, bottom-centred - matches the sidebar's version treatment.
        std::string verText = "v" + CURRENT_VERSION;
        float verW = ImGui::CalcTextSize(verText.c_str()).x;
        ImGui::SetCursorPos(ImVec2((win.x - verW) * 0.5f, win.y - (28.0f * dpiScale)));
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.48f, 1.0f), "%s", verText.c_str());

        ImGui::PopStyleVar();
        ImGui::End();
    }

    void InitTelemetryWorker() {
        telemetryWorker = std::thread([&]() {
            // The whole worker is guarded. It runs for the life of the app, doing
            // network I/O, JSON-ish parsing, PDH queries and process enumeration -
            // and an exception reaching the top of a thread calls std::terminate,
            // which would take the entire application down with no message and
            // nothing written to the log. Losing telemetry and auto-boost is bad;
            // losing the process is much worse, so at minimum it says what happened.
            try {
            (void)CoInitializeEx(0, COINIT_APARTMENTTHREADED);

            // Fetched into a local, sanitized here, and published to the shared
            // member exactly once at the end. See the declaration of
            // latestVersion for why mutating it in place was a data race.
            //
            // (The cache buster that used to be built on this line and then
            // never used has moved into Updater::HttpGetText, where it is
            // actually applied to the URL.)
            std::string fetched = OptiMax::Updater::FetchLatestVersion();

            // --- STRING SANITIZER ---
            fetched.erase(std::remove(fetched.begin(), fetched.end(), '\n'), fetched.end());
            fetched.erase(std::remove(fetched.begin(), fetched.end(), '\r'), fetched.end());
            fetched.erase(std::remove(fetched.begin(), fetched.end(), ' '), fetched.end());

            if (!fetched.empty() && (fetched[0] == 'v' || fetched[0] == 'V')) {
                fetched = fetched.substr(1);
            }

            // THE FIX: the old guard discarded anything containing "404" or "<",
            // which was a stand-in for "the HTTP status was never checked, so an
            // error page might be sitting in this string". The status is checked
            // properly now (Updater::HttpGetText), so this can be a real format
            // check instead - stricter against junk, and no longer able to throw
            // away a legitimate version number that happens to contain 4-0-4.
            bool wellFormed = !fetched.empty() && fetched.length() <= 15;
            if (wellFormed) {
                int digits = 0, dots = 0;
                for (unsigned char c : fetched) {
                    if (isdigit(c)) { digits++; }
                    else if (c == '.') { dots++; }
                    else { wellFormed = false; break; }
                }
                if (digits == 0 || dots > 3) wellFormed = false;
            }
            if (!wellFormed) {
                if (!fetched.empty()) {
                    SafeAddLog("UPDATER: Ignoring an unreadable version marker from the server.");
                }
                fetched.clear();
            }

            if (!fetched.empty()) {
                auto parseVersion = [](const std::string& v) {
                    std::vector<int> parts;
                    std::stringstream ss(v);
                    std::string item;
                    while (std::getline(ss, item, '.')) {
                        try { parts.push_back(std::stoi(item)); }
                        catch (...) { parts.push_back(0); }
                    }
                    while (parts.size() < 3) parts.push_back(0);
                    return parts;
                    };

                std::vector<int> currentV = parseVersion(CURRENT_VERSION);
                std::vector<int> targetV = parseVersion(fetched);

                bool isTargetNewer = false;
                for (size_t i = 0; i < 3; ++i) {
                    if (targetV[i] > currentV[i]) {
                        isTargetNewer = true;
                        break;
                    }
                    else if (targetV[i] < currentV[i]) {
                        break;
                    }
                }

                // Publish the string BEFORE raising the flag. The UI reads the
                // flag first, so this ordering guarantees that any frame which
                // sees the prompt also sees the version the prompt is about.
                {
                    std::lock_guard<std::mutex> lock(updateInfoMutex);
                    latestVersion = fetched;
                }

                if (isTargetNewer) {
                    showUpdatePrompt = true;
                    SafeAddLog("UPDATER: New build detected (v" + fetched + "). Prompting user.");
                }
                else {
                    SafeAddLog("UPDATER: System is running the latest optimal build.");
                }
            }
            else {
                SafeAddLog("UPDATER: Could not check for updates right now - the version marker "
                    "could not be read. Your installed version is unaffected.");
            }

            if (PdhOpenQuery(NULL, NULL, &sysQuery) == ERROR_SUCCESS) {
                PdhAddEnglishCounterW(sysQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &cpuTotal);
                PdhAddEnglishCounterW(sysQuery, L"\\GPU Engine(*)\\Utilization Percentage", NULL, &gpuTotal);
                PdhCollectQueryData(sysQuery);
            }

            while (g_isAppRunning) {
                bool currentAntiCheat = OptiMax::IsAntiCheatRunning();
                g_isAntiCheatActive = currentAntiCheat;

                // ==============================================================
                // FSE REGISTRY WATCHDOG (AUTO-HEAL)
                // ==============================================================
                // THE FIX: this watchdog is the ONLY thing that ever writes the FSE keys -
                // the toggle that turns the feature on just sets g_fseForced and logs
                // "Exclusive fullscreen preferred", trusting this loop to do the work.
                // But it only wrote when RegQueryValueExA SUCCEEDED and returned a value
                // other than 2. GameDVR_FSEBehaviorMode is not a value stock Windows
                // ships, so on any machine where it had never been set the query failed,
                // the watchdog did nothing, and the tweak never applied at all - for the
                // entire life of the install, with the toggle showing as on. It also
                // opened the key rather than creating it, so a machine with no
                // GameConfigStore key could never get one. A missing value now counts as
                // "not asserted" and goes through ForceFSE(), which creates what it needs.
                if (g_fseForced.load()) {
                    bool alreadyApplied = false;
                    bool valueExists = false;
                    HKEY hKey;
                    if (RegOpenKeyExA(HKEY_CURRENT_USER, "System\\GameConfigStore", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                        DWORD val = 0;
                        DWORD size = sizeof(DWORD);
                        DWORD type = 0;
                        if (RegQueryValueExA(hKey, "GameDVR_FSEBehaviorMode", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS
                            && type == REG_DWORD) {
                            valueExists = true;
                            alreadyApplied = (val == 2);
                        }
                        RegCloseKey(hKey);
                    }

                    if (!alreadyApplied) {
                        OptiMax::V3::ForceFSE();
                        SafeAddLog(valueExists
                            ? "WATCHDOG: Unauthorized FSE registry change detected. Re-asserting OptiMax locks."
                            : "TWEAK: Exclusive fullscreen preference written to the registry.");
                    }
                }
                // ==============================================================

                if (isProfilerRunning) {
                    if (sysQuery) {
                        PdhCollectQueryData(sysQuery);
                        PDH_FMT_COUNTERVALUE cv;
                        if (cpuTotal && PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &cv) == ERROR_SUCCESS) {
                            cachedCpuLoad = (int)cv.doubleValue;
                        }

                        if (gpuTotal) {
                            DWORD bS = 0, iC = 0;
                            PdhGetFormattedCounterArrayW(gpuTotal, PDH_FMT_DOUBLE, &bS, &iC, NULL);
                            if (bS > 0) {
                                std::vector<BYTE> buf(bS);
                                PDH_FMT_COUNTERVALUE_ITEM_W* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
                                if (PdhGetFormattedCounterArrayW(gpuTotal, PDH_FMT_DOUBLE, &bS, &iC, items) == ERROR_SUCCESS) {
                                    double maxG = 0.0;
                                    for (DWORD i = 0; i < iC; i++) {
                                        if (items[i].FmtValue.doubleValue > maxG) { maxG = items[i].FmtValue.doubleValue; }
                                    }
                                    cachedGpuLoad = (int)maxG;
                                }
                            }
                        }
                    }
                    cachedTimer = OptiMax::Profiler::GetCurrentTimerResolution();
                    cachedRam = OptiMax::Profiler::GetRAMUsage();
                    cachedDpcSpike = OptiMax::Profiler::GetMaxDPCSpikeMicroseconds();
                }

                bool curR = false;
                std::wstring activeGameName = L"";
                {
                    std::vector<std::string> localGames;
                    { std::lock_guard<std::mutex> lock(g_configMutex); localGames = g_targetGames; }

                    std::vector<std::wstring> wTargets;
                    wTargets.reserve(localGames.size());
                    for (const auto& game : localGames) {
                        wTargets.emplace_back(game.begin(), game.end());
                    }

                    std::wstring match = OptiMax::AutoBoost::FindRunningTarget(wTargets);

                    // Self-heal: an earlier build could auto-add an overlay/shell process
                    // (e.g. screenclippinghost.exe from a Win+Shift+S snip) to the library
                    // before those were excluded. Such an entry would match here and engage
                    // Auto-Boost immediately, re-creating the lag. If a match is actually a
                    // protected or known-non-game process, drop it from the library and
                    // ignore it.
                    if (!match.empty() && (OptiMax::IsProtectedProcess(match) || OptiMax::IsKnownNonGameApp(match))) {
                        std::string bad(match.begin(), match.end());
                        {
                            std::lock_guard<std::mutex> lock(g_configMutex);
                            g_targetGames.erase(std::remove(g_targetGames.begin(), g_targetGames.end(), bad), g_targetGames.end());
                        }
                        SavePersistentState();
                        SafeAddLog("LIBRARY: Removed non-game entry '" + bad + "' - it is a system/overlay process, not a game.");
                        match.clear();
                    }

                    if (!match.empty()) {
                        // A process the user explicitly put in the library is a trusted
                        // signal - engage immediately, no debounce needed.
                        curR = true;
                        activeGameName = match;
                        fsCandidate.clear();
                        fsStableCount = 0;
                    }

                    // Fallback: nothing in the manual list matched, but if the foreground
                    // window is fullscreen and isn't a protected/system process, treat it
                    // as a game too. Means zero-configuration detection for anything not
                    // explicitly added to the library. Debounced: the same fullscreen exe
                    // must persist for kFullscreenStableTicks consecutive checks, so a
                    // momentary fullscreen overlay (Win+Shift+S, alt-tab) never engages
                    // Auto-Boost and never gets auto-added to the library.
                    if (!curR) {
                        std::wstring fsGame = OptiMax::GetFullscreenForegroundGame();
                        if (fsGame.empty()) {
                            fsCandidate.clear();
                            fsStableCount = 0;
                        }
                        else {
                            if (fsGame == fsCandidate) fsStableCount++;
                            else { fsCandidate = fsGame; fsStableCount = 1; }
                        }

                        if (!fsGame.empty() && fsStableCount >= kFullscreenStableTicks) {
                            curR = true;
                            activeGameName = fsGame;

                            // THE FEATURE: auto-grow the library instead of requiring a manual
                            // "Browse for Game (.exe)" for every title. The first time a fullscreen
                            // game is detected that isn't already in the list, it gets added and
                            // persisted, exactly as if the user had browsed to it themselves. Every
                            // subsequent launch of that same game is then picked up directly by
                            // FindRunningTarget() above - this fallback path only ever fires once
                            // per new game.
                            std::string narrowName(fsGame.begin(), fsGame.end());
                            bool isNew = false;
                            {
                                std::lock_guard<std::mutex> lock(g_configMutex);
                                if (std::find(g_targetGames.begin(), g_targetGames.end(), narrowName) == g_targetGames.end()) {
                                    g_targetGames.push_back(narrowName);
                                    isNew = true;
                                }
                            }
                            if (isNew) {
                                SavePersistentState();
                                SafeAddLog("LIBRARY: Auto-added " + narrowName + " after detecting a fullscreen session.");
                            }
                        }
                    }
                }

                if (g_autoBoostEnabled.load()) {
                    if (curR && !g_isGameRunning) {
                        OptiMax::TrueStandbyPurge();
                        g_isCoreActive = true;
                        OptiMax::SetTimer(true, currentAntiCheat);
                        OptiMax::SetGPUPriorityHigh();
                        OptiMax::Power::EngageUltimatePower();
                        OptiMax::AdvancedPower::DisableCoreParking();
                        OptiMax::AdvancedPower::SetEPPToPerformance();
                        OptiMax::OptimizeNetworkLatency(true);

                        // ANTI-CHEAT SAFETY: the three tweaks below each OpenProcess() a
                        // handle directly ON the game (efficiency mode, P-core affinity,
                        // SMT/priority). Kernel anti-cheats - Vanguard especially - watch
                        // for third-party handles on the protected process, so we skip all
                        // of them whenever one is detected. currentAntiCheat is refreshed at
                        // the top of this loop. ForceFSE() further down is registry-only and
                        // never opens the process, so it is deliberately left running.
                        if (!activeGameName.empty() && currentAntiCheat) {
                            SafeAddLog("ANTI-CHEAT: Active - skipping per-game process tweaks (efficiency/P-core/SMT/priority) to avoid holding a handle on the game process.");
                        }

                        if (!activeGameName.empty() && !currentAntiCheat) {
                            OptiMax::DisableEfficiencyMode(activeGameName);
                        }

                        if (!activeGameName.empty() && !currentAntiCheat && ShouldLockPCoresForGame(activeGameName)) {
                            OptiMax::V3::LockToPCores(activeGameName);
                        }

                        // THE FEATURE: the per-game flags a community profile can carry,
                        // applied here because this is the moment the game's process
                        // actually exists to be tweaked. Reading them costs nothing when
                        // unset - a game with no entry in these maps takes no new code
                        // path at all.
                        if (!activeGameName.empty()) {
                            bool wantSmtOff = GetGameFlag(g_gameDisableSMT, activeGameName);
                            bool wantHighPriority = GetGameFlag(g_gameHighPriority, activeGameName);

                            // Same anti-cheat rule: ApplyGameProcessTweaks opens a handle on
                            // the game to flip SMT/priority, so it is gated too. The FSO block
                            // below is registry-only and stays.
                            if ((wantSmtOff || wantHighPriority) && !currentAntiCheat) {
                                OptiMax::V3::ApplyGameProcessTweaks(activeGameName, wantSmtOff, wantHighPriority);
                            }

                            // CAUTION - NOT A PER-GAME SETTING: ForceFSE() writes
                            // HKCU\System\GameConfigStore, which is a single machine-wide
                            // (per-user) switch. Windows exposes no per-executable
                            // Fullscreen Optimizations toggle that we can drive from here,
                            // so setting this flag on ONE game disables FSO for EVERY game
                            // on the system, and it stays disabled after that game exits
                            // (Settings > Restore Windows Defaults reverses it).
                            // It's kept because it is still the effect the flag asks for,
                            // but do not read the per-game map as implying per-game scope.
                            if (GetGameFlag(g_gameDisableFSO, activeGameName)) {
                                OptiMax::V3::ForceFSE();
                                SafeAddLog("GAME TWEAKS: Fullscreen Optimizations disabled - note this is a system-wide switch, not per-game.");
                            }
                        }

                        timerResolutionVerifiedThisSession = false;
                        SafeAddLog("AUTO-BOOST: Game detected - core tweaks applied.");
                    }
                    else if (!curR && g_isGameRunning) {
                        g_isCoreActive = false;
                        OptiMax::RevertOptimizations();

                        // THE FIX: engaging Auto-Boost calls DisableCoreParking() and
                        // SetEPPToPerformance() (above), but this exit path only called
                        // RevertOptimizations(), which covers the timer, the power plan
                        // and network latency - not those two. So the first game a user
                        // ever launched left core parking disabled and the energy
                        // performance preference pinned to maximum, permanently, on both
                        // AC and battery, while the log said "System Restored". Skipped
                        // when the standalone "Keep All Cores Awake" toggle is on, since
                        // then those settings are the user's own and not ours to undo.
                        if (!g_coresUnparked.load()) {
                            OptiMax::AdvancedPower::RestoreProcessorPowerSettings();
                        }
                        SafeAddLog("AUTO-BOOST: Game Closed. System Restored.");
                    }
                }
                // THE FIX: this used to live inside the "if (g_autoBoostEnabled)" block above,
                // so g_isGameRunning never updated at all when Auto-Boost was off - the RAM
                // cleaner below would think no game was ever running and could full-purge
                // straight through an active gaming session. Game detection now always runs,
                // and g_isGameRunning always reflects reality regardless of the Auto-Boost toggle.
                g_isGameRunning = curR;

                if (g_isCoreActive.load()) {
                    // THE FIX: SetTimer(true, currentAntiCheat) was immediately followed by
                    // a direct NtSetTimerResolution(5000, TRUE, ...) call, every second,
                    // unconditionally. That is precisely the call safe mode exists to avoid
                    // making while an anti-cheat is watching, so safe mode's timer path was
                    // decorative - the undocumented syscall went out anyway one line later.
                    // It was also a straight duplicate of what SetTimer already does on the
                    // non-safe path, and it acquired a timer request that nothing tracked,
                    // so nothing ever released it. SetTimer is now the only route in.
                    OptiMax::SetTimer(true, currentAntiCheat);

                    // Confirm the low-latency timer actually took effect (Windows has, in some
                    // builds, silently ignored or reverted manually-requested resolutions outside
                    // foreground/gaming contexts). Only check once per session to avoid log spam.
                    if (!timerResolutionVerifiedThisSession) {
                        double actualRes = OptiMax::Profiler::GetCurrentTimerResolution();
                        if (actualRes > 0.6) {
                            SafeAddLog("WARNING: Requested 0.5ms timer resolution but OS reports " +
                                std::to_string(actualRes) + "ms. Latency gains may not be applied.");
                        }
                        else {
                            SafeAddLog("TIMER: Verified " + std::to_string(actualRes) + "ms resolution active.");
                        }
                        timerResolutionVerifiedThisSession = true;
                    }
                }

                // --- SMART RAM CLEANER ---
                // No longer a hard "!g_isGameRunning" gate that disables cleaning for the
                // entire play session. Instead: full purge when idle, game-safe background-only
                // purge while a game is active, so RAM doesn't just climb to 90%+ uncontested
                // during long sessions, but the game's own memory is never touched.
                // Smart (background-only) and full purges get independent cooldowns since the
                // smart purge is cheaper and safer - it can never touch the game's own memory -
                // so there's no reason to make it wait as long as a full system purge.
                if (g_settingAutoRam.load() && !g_IsLowSpec) {
                    ULONGLONG now = GetTickCount64();
                    bool overThreshold = OptiMax::Profiler::GetRAMUsage() >= g_ramCleanThreshold.load();

                    if (curR) {
                        // THE FEATURE: a game flagged aggressiveRAMPurge gets a shorter
                        // cooldown between background cleans (15s instead of 30s). This is
                        // still the background-only clean either way - the game's own
                        // working set is never touched - so the only thing the flag changes
                        // is how often other processes get trimmed.
                        ULONGLONG smartCooldown = GetGameFlag(g_gameAggressiveRAMPurge, activeGameName) ? 15000ULL : 30000ULL;

                        if (overThreshold && (now - lastSmartPurgeTime > smartCooldown)) {
                            OptiMax::SmartBackgroundPurge(activeGameName);
                            SafeAddLog("RAM: Trimmed background apps (the running game was left untouched).");
                            lastSmartPurgeTime = now;
                        }
                    }
                    else {
                        if (overThreshold && (now - lastFullPurgeTime > 60000)) {
                            // Was TrueStandbyPurge(), which also purged the standby list
                            // (the OS file cache) every 60s. Dropping the whole file cache
                            // on a timer is what made the first Win+Shift+S / Start / Search
                            // after a purge cold-read from disk and stall. SmartBackgroundPurge
                            // trims background working sets (the actual RAM win) but leaves the
                            // standby list warm. The manual "Clean RAM Now" button still does
                            // the full standby purge for anyone who explicitly wants it.
                            OptiMax::SmartBackgroundPurge(L"");
                            SafeAddLog("RAM: Trimmed background apps (file cache left warm for responsiveness).");
                            lastFullPurgeTime = now;
                        }
                    }
                }

                Sleep(1000);
            }
            if (sysQuery) PdhCloseQuery(sysQuery);
            CoUninitialize();
            }
            catch (const std::exception& e) {
                SafeAddLog(std::string("SYSTEM ERROR: The background monitor stopped unexpectedly (") + e.what() +
                    "). Live stats and Auto-Boost are no longer updating - restart OptiMax to bring them back.");
            }
            catch (...) {
                SafeAddLog("SYSTEM ERROR: The background monitor stopped unexpectedly. Live stats and Auto-Boost are no longer updating - restart OptiMax to bring them back.");
            }
            });
    }

public:
    OptiMaxApp(HWND windowHandle, HINSTANCE hInst, int nCmdShow, bool isTrayBoot)
        : hwnd(windowHandle), hInstance(hInst), isHeterogeneousCPU(false), isAdmin(false), isD3DReady(true), dpiScale(1.0f), cardHeight(0.0f), toggleAnchorY(0.0f), bottomAnchorY(0.0f) {

        // STARTUP ORDER: Login -> Startup Phase -> App.
        // The startup phase used to run first, in parallel with nothing, and the
        // sign-in screen came after it. It's armed here instead and starts the
        // moment the launch screen is done with (signed in, session restored, or
        // continuing as a guest), so it reads as the app booting up *for you*
        // rather than as a delay before being asked who you are. A tray boot has
        // no visible startup phase at all.
        showSplash = false;
        splashPending = !isTrayBoot;
        splashStartTime = 0;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
        if (dpiScale <= 0.0f) dpiScale = 1.0f;

        m_fontRegular = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f * dpiScale);
        m_fontBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 24.0f * dpiScale);
        if (!m_fontRegular) m_fontRegular = io.Fonts->AddFontDefault();
        if (!m_fontBold) m_fontBold = m_fontRegular;

        // UI STYLING
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(16 * dpiScale, 16 * dpiScale);
        style.FramePadding = ImVec2(12 * dpiScale, 6 * dpiScale);
        style.ItemSpacing = ImVec2(10 * dpiScale, 12 * dpiScale);
        style.ChildRounding = 10.0f;
        style.FrameRounding = 6.0f;
        style.WindowBorderSize = 0.0f;
        style.FrameBorderSize = 1.0f;
        style.ScrollbarSize = 12.0f * dpiScale;

        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.02f, 0.03f, 1.0f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.04f, 0.09f, 0.40f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.40f, 0.15f, 0.80f, 0.40f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.02f, 0.08f, 0.98f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.40f, 0.15f, 0.80f, 0.40f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.25f, 0.95f, 0.80f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.65f, 0.35f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.98f, 1.0f);

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

        SafeAddLog("System initialized. UI Pipeline online.");

        // THE FIX: this scan sets g_IsAmdGpu (AMD driver whitelist, MSI injection
        // targeting) and contributes to g_IsLowSpec, but was never actually called
        // anywhere in the project - g_IsAmdGpu was permanently false regardless of
        // real hardware. Run it on a background thread (WMI queries aren't
        // instant) so it doesn't block the splash screen or UI thread, and so it
        // never shares a thread with GetRAMInfo()'s own COM initialization below
        // (mixing COINIT_MULTITHREADED and COINIT_APARTMENTTHREADED on the same
        // thread fails - keeping them on separate threads sidesteps that entirely).
        std::thread(OptiMax::Hardware::RunDeepWmiScan).detach();

        // Pull the community profile database once at startup so the Community
        // Profiles tab has something to show the first time it's opened.
        // StartSync() detaches its own worker, so this never blocks init.
        // Fetching is all it does - nothing here is applied to anything.
        OptiMax::CloudSync::StartSync();

        // ACCOUNTS: if a "remember me" session was stored, try to exchange it for
        // a live one before the user is ever shown the sign-in form. The check for
        // whether a token exists is local (registry + DPAPI, no network), so
        // wWinMain has already used it to pick the right initial window size -
        // meaning the common case never flashes the portrait layout.
        // The exchange itself is a network call, so it runs detached and the
        // launch screen shows "Restoring your session..." until it resolves.
        bool hadStoredSession = OptiMax::Auth::HasStoredSession();
        if (hadStoredSession) {
            OptiMax::Auth::TryRestoreSession();
        }
        // Mirror the decision wWinMain already made from the same check, so this
        // flag describes the window that actually exists.
        windowIsPortrait = !hadStoredSession;

        hwCPU = OptiMax::Hardware::GetCPUName();
        hwRAM = OptiMax::Hardware::GetRAMInfo();
        hwGPU = "Unknown GPU";
        hwOS = "Windows";

        // --- OS DETECTION MODULE ---
        //
        // THE FIX (two bugs, one of them fatal):
        //
        // 1. std::stoi THROWS. It was called on a raw registry buffer with no
        //    try/catch and no type check, in a constructor that runs on the main
        //    thread before the window exists. A CurrentBuildNumber that is absent,
        //    empty, non-numeric, or stored as anything other than REG_SZ threw
        //    std::invalid_argument straight out of the constructor - and an
        //    exception escaping there terminates the process at startup with no
        //    window, no message and (until the log file existed) no trace at all.
        //    That is the same silent-death failure mode as the D3D11 init bug: the
        //    user double-clicks the exe and nothing whatsoever happens.
        //
        // 2. Unterminated REG_SZ. Neither read checked the value type or
        //    NUL-terminated the buffer, and `hwOS = productName` on a 256-byte
        //    value with no terminator reads off the end of the stack buffer. This
        //    is the same defect already fixed in GetCPUName() and
        //    ReadRecordedTaskPath(); this was the last site still carrying it.
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char productName[256] = { 0 };
            DWORD type = 0;
            DWORD size = sizeof(productName) - 1;   // room for a terminator we add ourselves
            if (RegQueryValueExA(hKey, "ProductName", NULL, &type, (LPBYTE)productName, &size) == ERROR_SUCCESS
                && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                productName[size] = '\0';
                hwOS = productName;

                char buildNumber[64] = { 0 };
                type = 0;
                size = sizeof(buildNumber) - 1;
                if (RegQueryValueExA(hKey, "CurrentBuildNumber", NULL, &type, (LPBYTE)buildNumber, &size) == ERROR_SUCCESS
                    && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                    buildNumber[size] = '\0';

                    // Parsed by hand rather than with stoi: this must not be able to
                    // throw, and "unreadable build number" is not worth a crash when
                    // the only consequence is that one Windows 11 tweak stays hidden.
                    int build = 0;
                    bool haveDigits = false;
                    for (const char* p = buildNumber; *p; ++p) {
                        if (*p < '0' || *p > '9') { if (haveDigits) break; else continue; }
                        haveDigits = true;
                        if (build < 1000000) build = build * 10 + (*p - '0');
                    }

                    if (haveDigits) {
                        // Build 22000+ is Windows 11 regardless of the reported
                        // ProductName; this gates the Win11-only snappy-UI tweaks.
                        isWindows11 = (build >= 22000);

                        // Windows 11 API masking bypass
                        size_t win10 = hwOS.find("Windows 10");
                        if (build >= 22000 && win10 != std::string::npos) {
                            // Replace the "10" belonging to "Windows 10", not the
                            // first "10" anywhere in the string.
                            hwOS.replace(win10 + 8, 2, "11");
                        }
                    }
                }
            }
            RegCloseKey(hKey);
        }

        std::string cpuLower = hwCPU;
        std::transform(cpuLower.begin(), cpuLower.end(), cpuLower.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });
        if (cpuLower.find("intel") != std::string::npos && (cpuLower.find("12") != std::string::npos || cpuLower.find("13") != std::string::npos || cpuLower.find("14") != std::string::npos || cpuLower.find("ultra") != std::string::npos)) isHeterogeneousCPU = true;
        else if (cpuLower.find("ryzen") != std::string::npos && (cpuLower.find("x3d") != std::string::npos || cpuLower.find("7900") != std::string::npos || cpuLower.find("7950") != std::string::npos)) isHeterogeneousCPU = true;

        if (g_pd3dDevice != nullptr) {
            IDXGIDevice* pDXGIDevice = nullptr;
            if (SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice)) && pDXGIDevice != nullptr) {
                IDXGIAdapter* pDXGIAdapter = nullptr;
                if (SUCCEEDED(pDXGIDevice->GetAdapter(&pDXGIAdapter)) && pDXGIAdapter != nullptr) {
                    DXGI_ADAPTER_DESC adapterDesc;
                    if (SUCCEEDED(pDXGIAdapter->GetDesc(&adapterDesc))) {
                        char ch[256];
                        WideCharToMultiByte(CP_ACP, 0, adapterDesc.Description, -1, ch, 256, NULL, NULL);
                        hwGPU = ch;
                    }
                    pDXGIAdapter->Release();
                }
                pDXGIDevice->Release();
            }
        }

        isAdmin = OptiMax::IsElevated();
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

        // Card height has to fit: padding + bold title + a description that wraps
        // to up to three lines + the toggle + the button + padding. The old 195px
        // only fit a ONE-line description, which is why longer, more accurate
        // descriptions started running underneath the controls.
        cardHeight = 235.0f * dpiScale;

        // Placeholder values only - both anchors are recomputed every frame in
        // Run() from the real frame height, since that's the only place
        // ImGui::GetFrameHeight() is meaningful.
        toggleAnchorY = 0.0f;
        bottomAnchorY = 0.0f;

        InitTelemetryWorker();
    }

    ~OptiMaxApp() {
        g_isAppRunning = false;
        if (hwnd) Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (!OptiMax::IsExplorerRunning()) OptiMax::ToggleExplorer();

        OptiMax::RevertOptimizations();

        if (telemetryWorker.joinable()) telemetryWorker.join();
        if (g_benchThread.joinable()) g_benchThread.join();

        if (isD3DReady) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            CleanupDeviceD3D();
        }
    }

    int Run() {
        if (!isD3DReady || !hwnd) return 1;

        ImVec4 sexyPurple = ImVec4(0.65f, 0.35f, 1.0f, 1.0f);
        ImGuiWindowFlags staticCardFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        while (!done) {
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT) done = true;
            }
            if (done) break;

            // THE FIX (1 of 2): Sleep(100) here is a poll - it wakes 10x a second
            // to ask whether anything happened, forever, for a window that is in
            // the tray. MsgWaitForMultipleObjects blocks until a message actually
            // arrives, so a tray-resident session costs essentially nothing while
            // still waking instantly for the tray icon and for WM_QUIT. The
            // timeout is kept only as a safety net so this can never wait forever
            // on a message that never comes.
            if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) {
                ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
                continue;
            }

            float globalBtnHeight = ImGui::GetFrameHeight();

            // THE FIX: these two anchors used to be hardcoded offsets from the TOP of
            // a card (85px and 125px). That only ever worked while every card
            // description fit on exactly one line - a two-line description already ran
            // underneath the toggle, and a four-line one ran under the button too.
            // They're now measured back from the BOTTOM of the fixed-height card, so
            // the toggle and button land in the same place on every card no matter how
            // many lines the description above them wraps to. A short description just
            // leaves a little more breathing room in the middle.
            bottomAnchorY = cardHeight - ImGui::GetStyle().WindowPadding.y - globalBtnHeight;
            toggleAnchorY = bottomAnchorY - globalBtnHeight - ImGui::GetStyle().ItemSpacing.y;

            // ACCOUNTS: decode a newly downloaded or uploaded avatar into a GPU
            // texture. This has to happen here on the render thread - the auth
            // worker threads hand over raw bytes precisely because they must not
            // touch the D3D11 device themselves.
            if (OptiMax::Auth::g_avatarChanged.exchange(false)) {
                std::vector<unsigned char> avatarBytes = OptiMax::Auth::TakeAvatarBytes();
                if (avatarBytes.empty()) {
                    avatarTexture.Release();
                }
                else if (!OptiMax::Image::DecodeToTexture(g_pd3dDevice, avatarBytes, avatarTexture)) {
                    avatarTexture.Release();
                    SafeAddLog("ACCOUNT: That profile picture couldn't be displayed - the image format wasn't readable.");
                }
            }
            ULONGLONG currentTime = GetTickCount64();

            bool localAntiCheatActive = g_isAntiCheatActive.load();
            bool localBenchmarking = g_isBenchmarking.load();
            std::string safeLatestVer = GetLatestVersionSafe();

            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();

            // ORDER: Login -> Startup Phase -> App.
            bool formActive = AuthFormActive();

            if (!formActive) {
                // Past the launch screen: grow the window out to the landscape
                // layout the startup phase and main UI are designed for. Guarded by
                // windowIsPortrait so this fires exactly once per transition.
                if (windowIsPortrait) {
                    ApplyWindowLayout(false);
                    // Any typed password is dead weight in memory from here on.
                    SecureZeroMemory(authPassword, sizeof(authPassword));
                    SecureZeroMemory(authConfirm, sizeof(authConfirm));
                }
                // First frame past the launch screen - the startup phase begins here.
                if (splashPending) {
                    splashPending = false;
                    showSplash = true;
                    splashStartTime = currentTime;
                }
            }

            if (formActive) {
                // ACCOUNTS: the first thing anyone sees, unless a stored session
                // means there's nothing to ask.
                RenderLaunchScreen(currentTime, sexyPurple, globalBtnHeight);

                // A silent restore that failed lands here partway through the
                // startup phase. Re-arm it rather than burning it on a screen the
                // user is about to be pulled off of.
                if (showSplash) {
                    showSplash = false;
                    splashPending = true;
                }

                if (!windowIsPortrait) ApplyWindowLayout(true);
            }
            else if (showSplash || OptiMax::Auth::GetState() == OptiMax::Auth::State::Restoring) {
                // The Restoring clause is what keeps a slow silent refresh from
                // falling through into the main UI when it outlives the startup
                // phase's three seconds. The app must not draw itself as signed in
                // before it knows whether it is.
                if (splashStartTime == 0) splashStartTime = currentTime;

                float progress = (currentTime - splashStartTime) / 3000.0f;
                if (progress >= 1.0f) { showSplash = false; }

                float alpha = ImClamp(progress * 3.0f, 0.0f, 1.0f);
                ImVec4 fadedPurple = ImVec4(sexyPurple.x, sexyPurple.y, sexyPurple.z, alpha);

                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

                ImGui::Begin("Splash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);

                ImVec2 windowSize = ImGui::GetWindowSize();

                // Shared with the launch screen via DrawWordmark, so the logo
                // treatment on both screens can never drift apart.
                DrawWordmark(windowSize.x * 0.5f, windowSize.y * 0.40f, currentTime, alpha, sexyPurple);

                const char* targetStatus = "";
                int phaseTimeMS = 0;

                if (progress < 0.25f) { targetStatus = "> STARTING UP"; phaseTimeMS = (int)(currentTime - splashStartTime); }
                else if (progress < 0.50f) { targetStatus = "> STARTING MONITORS"; phaseTimeMS = (int)((currentTime - splashStartTime) - 750); }
                else if (progress < 0.75f) { targetStatus = "> LOADING GAME LIBRARY"; phaseTimeMS = (int)((currentTime - splashStartTime) - 1500); }
                else { targetStatus = "> READY"; phaseTimeMS = (int)((currentTime - splashStartTime) - 2250); }

                size_t len = strlen(targetStatus);
                size_t charsToShow = phaseTimeMS / 25;
                size_t safeChars = (charsToShow > len) ? len : charsToShow;

                std::string typeWriterStr = std::string(targetStatus).substr(0, safeChars);
                if (safeChars < len) typeWriterStr += (phaseTimeMS % 300 < 150) ? "_" : " ";
                else typeWriterStr += " [OK]";

                float statusWidth = ImGui::CalcTextSize(typeWriterStr.c_str()).x;
                ImGui::SetCursorPos(ImVec2((windowSize.x - statusWidth) * 0.5f, windowSize.y * 0.48f));
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, alpha), "%s", typeWriterStr.c_str());

                float barWidth = 300.0f * dpiScale;
                ImGui::SetCursorPos(ImVec2((windowSize.x - barWidth) * 0.5f, windowSize.y * 0.53f));
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fadedPurple);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.05f, 0.08f, alpha * 0.5f));
                ImGui::ProgressBar(progress, ImVec2(barWidth, 3.0f * dpiScale), "");
                ImGui::PopStyleColor(2);

                ImGui::End();
            }
            else {
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
                ImGui::Begin("OptiMax", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

                // exchange rather than test-then-clear: the flag is set on the
                // worker thread, so the two-step version could drop a request
                // that arrived between the read and the write.
                if (showUpdatePrompt.exchange(false)) {
                    ImGui::OpenPopup("Update Available");
                }

                ImGui::SetNextWindowSize(ImVec2(480 * dpiScale, 0));
                if (ImGui::BeginPopupModal("Update Available", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
                    float availW = ImGui::GetContentRegionAvail().x;

                    ImGui::Dummy(ImVec2(0, 5 * dpiScale));

                    if (m_fontBold) ImGui::PushFont(m_fontBold);
                    const char* hText = "UPDATE AVAILABLE";
                    float hW = ImGui::CalcTextSize(hText).x;
                    ImGui::SetCursorPosX((availW - hW) * 0.5f + ImGui::GetStyle().WindowPadding.x);
                    ImGui::TextColored(sexyPurple, hText);
                    if (m_fontBold) ImGui::PopFont();

                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    ImGui::TextWrapped("A newer version of OptiMax is available. Download and install it now?");
                    ImGui::Dummy(ImVec2(0, 10 * dpiScale));

                    ImGui::BeginChild("VersionBox", ImVec2(0, 90 * dpiScale), true, staticCardFlags);
                    ImGui::SetCursorPosY(15 * dpiScale);

                    float labelX = (ImGui::GetWindowWidth() - (150.0f * dpiScale)) * 0.5f;
                    float valueX = labelX + 110.0f * dpiScale;

                    ImGui::SetCursorPosX(labelX); ImGui::TextDisabled("Current Build:");
                    ImGui::SameLine(valueX); ImGui::Text("v%s", CURRENT_VERSION.c_str());
                    ImGui::Spacing();

                    ImGui::SetCursorPosX(labelX); ImGui::TextDisabled("Updated build:");
                    ImGui::SameLine(valueX);
                    if (safeLatestVer.empty()) ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Network Error");
                    else ImGui::TextColored(sexyPurple, "v%s", safeLatestVer.c_str());

                    ImGui::EndChild();
                    ImGui::Dummy(ImVec2(0, 15 * dpiScale));

                    float btnW = 215 * dpiScale;
                    float spacing = 15 * dpiScale;
                    float totalBtnW = (btnW * 2) + spacing;
                    ImGui::SetCursorPosX((availW - totalBtnW) * 0.5f + ImGui::GetStyle().WindowPadding.x);

                    if (safeLatestVer.empty()) ImGui::BeginDisabled();
                    if (PremiumButton("Download & Install", ImVec2(btnW, globalBtnHeight * 1.5f))) {
                        g_isDownloadingUpdate = true;
                        SafeAddLog("UPDATER: Starting download...");
                        // Guarded: an exception escaping a detached thread calls
                        // std::terminate. DownloadAndApplyUpdate does file I/O, hashing
                        // and string building, and it owns g_isDownloadingUpdate - so an
                        // unhandled throw would both kill the app and, had it survived,
                        // have left the UI stuck on "Downloading..." forever.
                        std::thread([]() {
                            try {
                                OptiMax::Updater::DownloadAndApplyUpdate();
                            }
                            catch (...) {
                                g_isDownloadingUpdate = false;
                                SafeAddLog("UPDATER ERROR: The update failed unexpectedly. Your installed version is unchanged.");
                            }
                            }).detach();
                        ImGui::CloseCurrentPopup();
                    }
                    if (safeLatestVer.empty()) ImGui::EndDisabled();

                    ImGui::SameLine(0, spacing);

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.55f, 1.0f));
                    if (ImGui::Button("Ignore Update", ImVec2(btnW, globalBtnHeight * 1.5f))) { ImGui::CloseCurrentPopup(); }
                    ImGui::PopStyleColor(4);

                    ImGui::Dummy(ImVec2(0, 5 * dpiScale));
                    ImGui::EndPopup();
                }

                // THE FEATURE: the X button no longer closes immediately - WM_CLOSE
                // just sets this flag instead of destroying the window (see WndProc).
                // This dialog is the only thing that actually decides what happens.
                if (g_showCloseConfirm.load()) {
                    ImGui::OpenPopup("Close OptiMax?");
                    g_showCloseConfirm = false;
                }

                ImGui::SetNextWindowSize(ImVec2(420 * dpiScale, 0));
                if (ImGui::BeginPopupModal("Close OptiMax?", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
                    ImGui::TextWrapped("Do you want to minimize OptiMax to the tray, or close it completely?");
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Closing completely reverts all active optimizations.");
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    float availW = ImGui::GetContentRegionAvail().x;
                    float btnW = (availW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

                    if (PremiumButton("Minimize to Tray", ImVec2(btnW, globalBtnHeight * 1.3f))) {
                        // THE FEATURE: this choice is remembered - the close dialog won't
                        // show again. Future X presses go straight to tray; the only way
                        // to fully exit becomes the tray icon's right-click "Exit App".
                        // Can be reset from Settings if the user wants the dialog back.
                        g_closeBehavior = 1; SavePersistentState();
                        MinimizeToTray(hwnd);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (PremiumButton("Exit Completely", ImVec2(btnW, globalBtnHeight * 1.3f))) {
                        DestroyWindow(hwnd);
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::Spacing();
                    if (ImGui::Button("Cancel", ImVec2(-1, globalBtnHeight))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // --- FIRST RUN: START WITH WINDOWS ---
                // Replaces the old behaviour of writing the Run key unannounced on
                // first launch. Shown here rather than at startup so it lands after
                // the launch screen and boot sequence, on the app itself.
                if (g_askAutoStartOnFirstRun.load()) {
                    ImGui::OpenPopup("Start with Windows?");
                    g_askAutoStartOnFirstRun = false;
                }

                ImGui::SetNextWindowSize(ImVec2(440 * dpiScale, 0));
                if (ImGui::BeginPopupModal("Start with Windows?", NULL,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {

                    if (m_fontBold) ImGui::PushFont(m_fontBold);
                    ImGui::TextColored(sexyPurple, "Start OptiMax with Windows?");
                    if (m_fontBold) ImGui::PopFont();

                    ImGui::Spacing();
                    ImGui::TextWrapped("OptiMax can launch to the tray when you sign in, so your saved "
                        "optimizations are applied before you open a game.");
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f),
                        "You can change this any time in Settings.");
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    float availW = ImGui::GetContentRegionAvail().x;
                    float btnW = (availW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

                    if (PremiumButton("Yes, start with Windows", ImVec2(btnW, globalBtnHeight * 1.3f))) {
                        // Exactly what the old first-run code did, minus the assuming.
                        SetStartupRegistry(true);
                        g_settingAutoStart = true;
                        g_autoStartAsked = true;
                        SavePersistentState();
                        SafeAddLog("SYSTEM: Start with Windows enabled.");
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Not now", ImVec2(btnW, globalBtnHeight * 1.3f))) {
                        // Recorded so declining sticks - otherwise this would ask
                        // again on every launch, which is its own kind of nagware.
                        g_autoStartAsked = true;
                        SavePersistentState();
                        SafeAddLog("SYSTEM: Start with Windows left off - enable it any time in Settings.");
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (g_isDownloadingUpdate.load()) { ImGui::OpenPopup("Downloading Update..."); }

                if (ImGui::BeginPopupModal("Downloading Update...", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
                    float availW = ImGui::GetContentRegionAvail().x;
                    ImGui::Dummy(ImVec2(0, 10 * dpiScale));

                    if (m_fontBold) ImGui::PushFont(m_fontBold);
                    const char* downStr = "UPDATING...";
                    float downW = ImGui::CalcTextSize(downStr).x;
                    ImGui::SetCursorPosX((availW - downW) * 0.5f + ImGui::GetStyle().WindowPadding.x);
                    ImGui::TextColored(sexyPurple, downStr);
                    if (m_fontBold) ImGui::PopFont();

                    ImGui::Spacing();
                    const char* waitStr = "Downloading and verifying update. Please wait.";
                    float waitW = ImGui::CalcTextSize(waitStr).x;
                    ImGui::SetCursorPosX((availW - waitW) * 0.5f + ImGui::GetStyle().WindowPadding.x);
                    ImGui::TextDisabled("%s", waitStr);
                    ImGui::Dummy(ImVec2(0, 10 * dpiScale));
                    ImGui::EndPopup();
                }

                // --- SIDEBAR MENU ---
                ImGui::BeginChild("Sidebar", ImVec2(280 * dpiScale, 0), true);
                ImGui::SetCursorPosY(25.0f * dpiScale);

                if (m_fontBold) ImGui::PushFont(m_fontBold);
                float sidebarWidth = ImGui::GetWindowSize().x;
                float textWidth = ImGui::CalcTextSize("OPTIMAX PRO").x;
                ImGui::SetCursorPosX((sidebarWidth - textWidth) * 0.5f);
                ImGui::TextColored(sexyPurple, "OPTIMAX PRO");
                if (m_fontBold) ImGui::PopFont();

                ImGui::SetCursorPosY(65.0f * dpiScale); ImGui::Separator(); ImGui::SetCursorPosY(80.0f * dpiScale);

                ImGuiTreeNodeFlags categoryFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth;

                if (ImGui::TreeNodeEx("##Tweaks", categoryFlags, "SYSTEM TWEAKS")) {
                    if (ImGui::Selectable("  Dashboard", activeTab == 0)) activeTab = 0;
                    if (ImGui::Selectable("  Power & Timers", activeTab == 1)) activeTab = 1;
                    if (ImGui::Selectable("  Services & Cleanup", activeTab == 2)) activeTab = 2;
                    if (ImGui::Selectable("  Debloat", activeTab == 10)) activeTab = 10;
                    if (ImGui::Selectable("  Discord Debloat", activeTab == 11)) activeTab = 11;
                    ImGui::TreePop();
                }
                ImGui::Spacing();

                if (ImGui::TreeNodeEx("##Library", categoryFlags, "GAME LIBRARY")) {
                    if (ImGui::Selectable("  My Games", activeTab == 8)) activeTab = 8;
                    if (ImGui::Selectable("  Community Profiles", activeTab == 9)) activeTab = 9;
                    ImGui::TreePop();
                }
                ImGui::Spacing();

                if (ImGui::TreeNodeEx("##Monitors", categoryFlags, "MONITORING")) {
                    if (ImGui::Selectable("  Latency Test", activeTab == 3)) activeTab = 3;
                    if (ImGui::Selectable("  Live Monitor", activeTab == 4)) activeTab = 4;
                    if (ImGui::Selectable("  Application Logs", activeTab == 6)) activeTab = 6;
                    ImGui::TreePop();
                }
                ImGui::Spacing();

                if (ImGui::TreeNodeEx("##V3", categoryFlags, "ADVANCED")) {
                    if (ImGui::Selectable("  Experimental Tweaks", activeTab == 7)) activeTab = 7;
                    ImGui::TreePop();
                }

                // --- ACCOUNT ROW, SETTINGS, VERSION (anchored to the sidebar bottom) ---
                // THE FIX: Settings used to sit at -75 with the separator at -90.
                // The account row needs that space, so everything moved up: separator
                // -160, account row -150 (56 tall), Settings -75, version -35. Laid
                // out from the bottom so nothing overlaps at any window height.
                // The row is 56 rather than 48 so the picture inside it can be big
                // enough to actually recognise.
                float sidebarH = ImGui::GetWindowHeight();

                ImGui::SetCursorPosY(sidebarH - (160.0f * dpiScale));
                ImGui::Separator();

                {
                    namespace Auth = OptiMax::Auth;
                    bool isGuest = (Auth::GetState() == Auth::State::Guest);
                    std::string uname = Auth::GetUsername();
                    if (uname.empty()) uname = isGuest ? "Guest" : "Account";

                    float rowH = 56.0f * dpiScale;
                    float rowW = ImGui::GetWindowWidth() - (24.0f * dpiScale);

                    ImGui::SetCursorPos(ImVec2(12.0f * dpiScale, sidebarH - (150.0f * dpiScale)));

                    ImGui::PushID("account_row");
                    ImVec2 rowStart = ImGui::GetCursorScreenPos();
                    bool rowClicked = ImGui::Selectable("##accountrow", false, 0, ImVec2(rowW, rowH));

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float avSize = 44.0f * dpiScale;
                    ImVec2 avMin = ImVec2(rowStart.x + (7.0f * dpiScale), rowStart.y + (rowH - avSize) * 0.5f);
                    ImVec2 avMax = ImVec2(avMin.x + avSize, avMin.y + avSize);

                    if (avatarTexture.valid()) {
                        ImVec2 uv0, uv1;
                        AvatarCropUV(avatarTexture, uv0, uv1);
                        dl->AddImageRounded((ImTextureID)(size_t)avatarTexture.srv,
                            avMin, avMax, uv0, uv1,
                            IM_COL32_WHITE, avSize * 0.5f);
                    }
                    else {
                        // Placeholder: filled circle with the first initial, so the row
                        // still reads as an account before any picture is set.
                        ImVec2 center = ImVec2((avMin.x + avMax.x) * 0.5f, (avMin.y + avMax.y) * 0.5f);
                        dl->AddCircleFilled(center, avSize * 0.5f,
                            isGuest ? IM_COL32(60, 60, 70, 255) : IM_COL32(90, 40, 150, 255));
                        dl->AddCircle(center, avSize * 0.5f, IM_COL32(150, 90, 220, 180), 0, 1.5f);

                        char initial[2] = { (char)toupper((unsigned char)uname[0]), 0 };
                        ImFont* initFont = m_fontBold ? m_fontBold : m_fontRegular;
                        float initSize = avSize * 0.45f;
                        ImVec2 ts = initFont
                            ? initFont->CalcTextSizeA(initSize, FLT_MAX, 0.0f, initial)
                            : ImGui::CalcTextSize(initial);
                        dl->AddText(initFont, initSize,
                            ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
                            IM_COL32(235, 235, 245, 255), initial);
                    }

                    float textX = avMax.x + (10.0f * dpiScale);
                    ImVec2 nameSize = ImGui::CalcTextSize(uname.c_str());
                    dl->AddText(ImVec2(textX, rowStart.y + (rowH * 0.5f) - nameSize.y - (1.0f * dpiScale)),
                        IM_COL32(240, 240, 248, 255), uname.c_str());

                    const char* subLabel = isGuest ? "Not signed in" : "Signed in";
                    dl->AddText(ImVec2(textX, rowStart.y + (rowH * 0.5f) + (2.0f * dpiScale)),
                        IM_COL32(130, 130, 145, 255), subLabel);

                    ImGui::PopID();

                    if (rowClicked) ImGui::OpenPopup("AccountMenu");

                    if (ImGui::BeginPopup("AccountMenu")) {
                        if (isGuest) {
                            ImGui::TextDisabled("Not signed in");
                            ImGui::Separator();
                            if (ImGui::MenuItem("Sign In / Create Account")) {
                                // Drop back to the launch screen; the render loop
                                // handles resizing the window back down for it.
                                Auth::g_state = (int)Auth::State::SignedOut;
                                Auth::ClearMessages();
                                authLocalError.clear();
                            }
                        }
                        else {
                            std::string email = Auth::GetEmail();
                            if (!email.empty()) ImGui::TextDisabled("%s", email.c_str());
                            ImGui::Separator();
                            if (ImGui::MenuItem("Edit Profile")) {
                                std::string current = Auth::GetUsername();
                                strncpy_s(editUsername, sizeof(editUsername), current.c_str(), _TRUNCATE);
                                Auth::ClearMessages();
                                openEditProfile = true;
                            }
                            if (ImGui::MenuItem("Sign Out")) {
                                Auth::SignOut();
                                avatarTexture.Release();
                                authLocalError.clear();
                                SecureZeroMemory(authPassword, sizeof(authPassword));
                                SecureZeroMemory(authConfirm, sizeof(authConfirm));
                            }
                        }
                        ImGui::EndPopup();
                    }
                }

                ImGui::SetCursorPos(ImVec2(16.0f * dpiScale, sidebarH - (75.0f * dpiScale)));
                if (ImGui::Selectable(" Settings", activeTab == 5)) activeTab = 5;

                ImGui::SetCursorPos(ImVec2(16.0f * dpiScale, sidebarH - (35.0f * dpiScale)));
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), ("v" + CURRENT_VERSION).c_str());
                ImGui::EndChild(); ImGui::SameLine();

                // --- EDIT PROFILE MODAL ---
                // Opened from the sidebar account menu. Rendered here at the main
                // window level rather than inside the sidebar child, so it centres
                // over the whole app the way the other modals do.
                if (openEditProfile) {
                    ImGui::OpenPopup("Edit Profile");
                    openEditProfile = false;
                }

                // Widened from 460 to give the 256px picture sensible margins.
                ImGui::SetNextWindowSize(ImVec2(520 * dpiScale, 0));
                if (ImGui::BeginPopupModal("Edit Profile", NULL,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
                    namespace Auth = OptiMax::Auth;
                    bool authBusy = Auth::IsBusy();

                    float availW = ImGui::GetContentRegionAvail().x;

                    // Live preview of the current picture, same circular treatment
                    // as the sidebar row. This is the one place the user is
                    // actually looking AT their picture rather than at a label
                    // next to it, so it's worth real screen space.
                    float previewSize = 256.0f * dpiScale;
                    ImVec2 previewPos = ImGui::GetCursorScreenPos();
                    previewPos.x += (availW - previewSize) * 0.5f;
                    ImDrawList* pdl = ImGui::GetWindowDrawList();
                    ImVec2 pMin = previewPos;
                    ImVec2 pMax = ImVec2(pMin.x + previewSize, pMin.y + previewSize);

                    if (avatarTexture.valid()) {
                        ImVec2 uv0, uv1;
                        AvatarCropUV(avatarTexture, uv0, uv1);
                        pdl->AddImageRounded((ImTextureID)(size_t)avatarTexture.srv,
                            pMin, pMax, uv0, uv1,
                            IM_COL32_WHITE, previewSize * 0.5f);
                    }
                    else {
                        ImVec2 c = ImVec2((pMin.x + pMax.x) * 0.5f, (pMin.y + pMax.y) * 0.5f);
                        pdl->AddCircleFilled(c, previewSize * 0.5f, IM_COL32(90, 40, 150, 255));
                        pdl->AddCircle(c, previewSize * 0.5f, IM_COL32(150, 90, 220, 180), 0, 2.0f);
                        std::string un = Auth::GetUsername();
                        char initial[2] = { (char)(un.empty() ? 'A' : toupper((unsigned char)un[0])), 0 };

                        // Scaled to the circle rather than left at the body text
                        // size, which looked lost in a 128px disc.
                        ImFont* initFont = m_fontBold ? m_fontBold : m_fontRegular;
                        float initSize = previewSize * 0.42f;
                        ImVec2 ts = initFont
                            ? initFont->CalcTextSizeA(initSize, FLT_MAX, 0.0f, initial)
                            : ImGui::CalcTextSize(initial);
                        pdl->AddText(initFont, initSize,
                            ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                            IM_COL32(235, 235, 245, 255), initial);
                    }
                    ImGui::Dummy(ImVec2(availW, previewSize + (10.0f * dpiScale)));

                    if (authBusy) ImGui::BeginDisabled();

                    float pickW = 220.0f * dpiScale;
                    ImGui::SetCursorPosX((availW - pickW) * 0.5f + ImGui::GetStyle().WindowPadding.x);
                    if (PremiumButton("Change Picture...", ImVec2(pickW, globalBtnHeight))) {
                        std::wstring imgPath = OptiMax::FileDialog::SelectImageFile(hwnd);
                        if (!imgPath.empty()) Auth::UploadAvatar(imgPath);
                    }

                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    ImGui::TextDisabled("Username");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::InputText("##editUsername", editUsername, sizeof(editUsername));

                    if (authBusy) ImGui::EndDisabled();

                    // Same two-channel treatment as the launch screen: red only
                    // when something actually failed.
                    std::string profileError = Auth::GetLastError();
                    std::string profileNotice = profileError.empty() ? Auth::GetLastNotice() : std::string();
                    if (!profileError.empty() || !profileNotice.empty()) {
                        ImGui::Spacing();
                        ImGui::PushTextWrapPos(0.0f);
                        if (!profileError.empty()) {
                            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", profileError.c_str());
                        }
                        else {
                            ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.68f, 1.0f), "%s", profileNotice.c_str());
                        }
                        ImGui::PopTextWrapPos();
                    }
                    if (authBusy) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.65f, 0.35f, 1.0f, 1.0f), "Saving...");
                    }

                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    float eBtnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

                    if (authBusy) ImGui::BeginDisabled();
                    if (PremiumButton("Save", ImVec2(eBtnW, globalBtnHeight))) {
                        std::string newName(editUsername);
                        std::string trimmedError;

                        // Same rules the create-account form enforces, so a rename
                        // can't produce a username signup would have rejected.
                        if (newName.size() < 3) trimmedError = "Pick a username of at least 3 characters.";
                        else if (newName.size() > 24) trimmedError = "Usernames can be at most 24 characters.";
                        else {
                            for (unsigned char ch : newName) {
                                if (!isalnum(ch) && ch != '_' && ch != '-') {
                                    trimmedError = "Usernames can only use letters, numbers, hyphens and underscores.";
                                    break;
                                }
                            }
                        }

                        if (!trimmedError.empty()) {
                            OptiMax::Auth::detail::SetError(trimmedError);
                        }
                        else if (newName == Auth::GetUsername()) {
                            ImGui::CloseCurrentPopup();   // nothing changed
                        }
                        else {
                            Auth::UpdateUsername(newName);
                        }
                    }
                    if (authBusy) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (ImGui::Button("Close", ImVec2(eBtnW, globalBtnHeight))) {
                        Auth::ClearMessages();
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                // --- MAIN CONTENT AREA ---
                ImGui::BeginChild("Main", ImVec2(0, 0), false);
                ImGui::SetCursorPosY(25.0f * dpiScale);

                if (m_fontBold) ImGui::PushFont(m_fontBold);
                if (activeTab == 0) ImGui::TextColored(sexyPurple, "  Dashboard");
                else if (activeTab == 1) ImGui::TextColored(sexyPurple, "  Power & Timer Settings");
                else if (activeTab == 2) ImGui::TextColored(sexyPurple, "  Background Services & Cleanup");
                else if (activeTab == 3) ImGui::TextColored(sexyPurple, "  DPC Latency Test");
                else if (activeTab == 4) ImGui::TextColored(sexyPurple, "  Live System Monitor");
                else if (activeTab == 5) ImGui::TextColored(sexyPurple, "  Settings");
                else if (activeTab == 6) ImGui::TextColored(sexyPurple, "  Application Logs");
                else if (activeTab == 7) ImGui::TextColored(sexyPurple, "  Experimental Tweaks");
                else if (activeTab == 8) ImGui::TextColored(sexyPurple, "  My Games");
                else if (activeTab == 9) ImGui::TextColored(sexyPurple, "  Community Profiles");
                else if (activeTab == 10) ImGui::TextColored(sexyPurple, "  Windows Debloat");
                else if (activeTab == 11) ImGui::TextColored(sexyPurple, "  Discord Debloat");
                if (m_fontBold) ImGui::PopFont();

                ImGui::SetCursorPosY(65.0f * dpiScale); ImGui::Separator(); ImGui::SetCursorPosY(80.0f * dpiScale);

                if (activeTab == 0) {
                    // Banner heights: 16 padding + 24 title + 12 spacing + two 20px text
                    // lines + 16 padding = 108. The old 70px clipped the bottom of even a
                    // single-line message, and these messages are now two lines.
                    const float bannerHeight = 115.0f * dpiScale;

                    if (!isAdmin) {
                        ImGui::BeginChild("Admin_Banner", ImVec2(0, bannerHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "[!] ADMINISTRATOR RIGHTS NEEDED"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("These tweaks change system-wide settings, so OptiMax needs to run as administrator. Restart it with \"Run as administrator\" to enable them."); ImGui::EndChild(); ImGui::BeginDisabled();
                    }

                    if (localAntiCheatActive) {
                        ImGui::BeginChild("AC_Banner", ImVec2(0, bannerHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold);
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "[!] ANTI-CHEAT RUNNING");
                        if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Switching to standard Windows timer APIs while an anti-cheat is active, to avoid tripping its checks.");
                        ImGui::EndChild();
                    }

                    if (g_IsLowSpec) {
                        ImGui::BeginChild("Spec_Banner", ImVec2(0, bannerHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold);
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "[!] LIMITED HARDWARE DETECTED");
                        if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("A mechanical hard drive or under 16GB of RAM was found. Automatic RAM cleaning is disabled, because on this hardware it costs more in disk reads than it frees.");
                        ImGui::EndChild();
                    }

                    if (ImGui::BeginTable("DashboardGrid", 2, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("Specs_Card", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "System Hardware Config"); if (m_fontBold) ImGui::PopFont();
                        ImGui::Dummy(ImVec2(0, 4.0f * dpiScale));
                        if (ImGui::BeginTable("SpecTable", 2, ImGuiTableFlags_SizingStretchProp)) {
                            ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 60.0f * dpiScale); ImGui::TableSetupColumn("R", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("OS:"); ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", hwOS.c_str());
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("CPU:"); ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", hwCPU.c_str());
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("GPU:"); ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", hwGPU.c_str());
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("RAM:"); ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", hwRAM.c_str());
                            ImGui::EndTable();
                        }
                        ImGui::EndChild();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("C_AutoBoost", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Automatic Game Detection"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Applies the core tweaks when a game starts and reverts them when it closes.");

                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempAutoBoost = g_autoBoostEnabled.load();
                        if (PremiumToggle("Auto-Boost", &tempAutoBoost)) {
                            g_autoBoostEnabled = tempAutoBoost;
                            SavePersistentState();
                            SafeAddLog(tempAutoBoost ? "SETTINGS: Auto-Boost enabled." : "SETTINGS: Auto-Boost disabled.");
                        }

                        ImGui::SetCursorPosY(bottomAnchorY);
                        size_t libCount = 0; { std::lock_guard<std::mutex> lock(g_configMutex); libCount = g_targetGames.size(); }
                        char libBtnText[64]; sprintf_s(libBtnText, "Edit Game Library (%zu game(s))", libCount);
                        if (PremiumButton(libBtnText, ImVec2(-1, globalBtnHeight))) { activeTab = 8; }
                        ImGui::EndChild();

                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("C1", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Core System Tweaks"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Sets the system timer to 0.5 ms, switches to the Ultimate Performance power plan, raises GPU interrupt priority, and lifts Windows' network throttling.");

                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempCore = g_isCoreActive.load();
                        bool coreToggled = PremiumToggle("Enable Core Tweaks", &tempCore);

                        ImGui::SetCursorPosY(bottomAnchorY);
                        bool coreButtoned = PremiumButton(tempCore ? "Revert Core Tweaks" : "Apply Core Tweaks", ImVec2(-1, globalBtnHeight));

                        if (coreToggled || coreButtoned) {
                            if (coreButtoned) tempCore = !tempCore;

                            if (tempCore) {
                                RestorePointBeforeTweak();
                                g_isCoreActive = true; SavePersistentState();
                                OptiMax::SetTimer(true, localAntiCheatActive); OptiMax::SetGPUPriorityHigh();
                                OptiMax::Power::EngageUltimatePower(); OptiMax::AdvancedPower::DisableCoreParking();
                                OptiMax::AdvancedPower::SetEPPToPerformance(); OptiMax::OptimizeNetworkLatency(true);
                                SafeAddLog("SYSTEM: Core system tweaks applied.");
                            }
                            else {
                                g_isCoreActive = false; SavePersistentState();
                                OptiMax::RevertOptimizations();
                                // Applying Core Tweaks disables core parking and pins EPP
                                // to maximum performance; RevertOptimizations() undoes
                                // neither. Left alone when the standalone "Keep All Cores
                                // Awake" toggle owns those settings.
                                if (!g_coresUnparked.load()) {
                                    OptiMax::AdvancedPower::RestoreProcessorPowerSettings();
                                }
                                SafeAddLog("SYSTEM: Core system tweaks reverted.");
                            }
                        }
                        ImGui::EndChild();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("C_RAM", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "RAM Cleaner"); if (m_fontBold) ImGui::PopFont();
                        // Honest framing per the accuracy audit: trimming working sets does
                        // not make a system with spare RAM faster - Windows just pages the
                        // data back in on next use. The real benefit is narrower than the
                        // old copy implied, so say what it actually is.
                        ImGui::TextWrapped("Asks background apps to release unused memory. Won't speed up a PC that has RAM to spare - it only helps avoid pagefile thrashing when memory runs short.");

                        ImGui::SetCursorPosY(toggleAnchorY);
                        if (g_IsLowSpec) ImGui::BeginDisabled();
                        bool toggleState = g_settingAutoRam.load();
                        if (PremiumToggle("Clean Automatically  ", &toggleState)) {
                            g_settingAutoRam = toggleState; SavePersistentState();
                            SafeAddLog(toggleState ? "SETTINGS: Automatic RAM cleaning enabled." : "SETTINGS: Automatic RAM cleaning disabled.");
                        }

                        ImGui::SetCursorPosY(bottomAnchorY);
                        if (PremiumButton("Clean RAM Now", ImVec2(-1, globalBtnHeight))) { OptiMax::TrueStandbyPurge(); }
                        if (g_IsLowSpec) ImGui::EndDisabled();
                        ImGui::EndChild();

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("C2", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold);
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Background App Throttling");
                        if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Lowers CPU, disk, and memory priority for every non-essential background app, so they compete less with the game.");

                        ImGui::SetCursorPosY(toggleAnchorY);

                        bool tempIso = g_isolationActive.load();
                        ImGui::PushID("toggle_iso");
                        bool isoToggled = PremiumToggle("Throttle Background Apps", &tempIso);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        bool isoButtoned = PremiumButton(tempIso ? "Stop Throttling" : "Start Throttling", ImVec2(-1, globalBtnHeight));

                        if (isoToggled || isoButtoned) {
                            if (isoButtoned) tempIso = !tempIso;

                            g_isolationActive = tempIso;

                            if (tempIso) {
                                ImGui::OpenPopup("Throttle Background Apps?");
                            }
                            else {
                                SavePersistentState();
                                RevertIsolationPriorities();
                                SafeAddLog("SYSTEM: Background app throttling turned off. Priorities restored to normal.");
                            }
                        }
                        ImGui::EndChild();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("C3", ImVec2(0, cardHeight), true, staticCardFlags);
                        bool expRunning = OptiMax::IsExplorerRunning();
                        bool shellOptimized = !expRunning;
                        if (m_fontBold) ImGui::PushFont(m_fontBold);
                        ImGui::TextColored(sexyPurple, "Explorer Shell");
                        if (m_fontBold) ImGui::PopFont();
                        // Honest framing per the accuracy audit: this is a hygiene/resource
                        // tweak, not an FPS feature. Explorer sits near-idle when you aren't
                        // interacting with it, and DWM - not Explorer - does the compositing.
                        ImGui::TextWrapped("Closes the desktop and taskbar to free their memory. Expect no frame rate change - Explorer sits idle while you play, and compositing is a separate process.");

                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool shellToggled = PremiumToggle("Close Explorer Shell", &shellOptimized);

                        ImGui::SetCursorPosY(bottomAnchorY);
                        bool shellButtoned = PremiumButton(expRunning ? "Close Explorer" : "Restart Explorer", ImVec2(-1, globalBtnHeight));

                        if (shellToggled || shellButtoned) {
                            OptiMax::ToggleExplorer();
                            SafeAddLog(expRunning ? "SYSTEM: Explorer shell closed." : "SYSTEM: Explorer shell restarted.");
                        }
                        ImGui::EndChild();

                        ImGui::EndTable();
                    }

                    ImGui::Spacing();
                    ImGui::BeginChild("C4", ImVec2(0, 75.0f * dpiScale), true, staticCardFlags);
                    ImGui::SetCursorPosY((75.0f * dpiScale - globalBtnHeight) * 0.5f);
                    if (PremiumButton("Turn Off All Tweaks", ImVec2(-1, globalBtnHeight))) {
                        // THE FIX: this cleared twelve state flags but only ever called
                        // RevertOptimizations() and RevertIsolationPriorities(), which
                        // between them undo the timer, the power plan, network latency
                        // and background priorities - and nothing else. The other eight
                        // tweaks it "turned off" (core parking, boot timers, disabled
                        // services, MSI mode, TCP latency, exclusive fullscreen, VBS and
                        // GPU interrupt pinning) stayed applied to the system while the
                        // app forgot it had ever applied them. The worst case is VBS:
                        // the user was told everything was off, the toggle showed
                        // Memory Integrity as back on, and it was still disabled with
                        // nothing left recording that fact. Clearing a flag and undoing
                        // the change it represents have to happen together, so this now
                        // reverts everything it claims to.
                        // Read each flag BEFORE clearing it, and revert only what was
                        // actually on. Reverting unconditionally would reach past what
                        // this app applied - re-enabling and starting DiagTrack, SysMain
                        // and WSearch for someone who had disabled those themselves and
                        // never touched OptiMax's debloat page, for instance.
                        const bool hadCores = g_coresUnparked.load();
                        const bool hadBoot = g_bootOptimized.load();
                        const bool hadServices = g_servicesSanitized.load();
                        const bool hadMsi = g_msiEnabled.load();
                        const bool hadNet = g_networkTweaked.load();
                        const bool hadFse = g_fseForced.load();
                        const bool hadVbs = g_vbsDisabled.load();
                        const bool hadIrq = g_irqPinned.load();
                        const bool needsRestart = hadBoot || hadMsi || hadVbs || hadIrq;

                        g_isCoreActive = false; g_autoBoostEnabled = false;
                        g_coresUnparked = false; g_bootOptimized = false; g_servicesSanitized = false;
                        g_isolationActive = false; g_pCoresLocked = false;
                        g_msiEnabled = false; g_networkTweaked = false; g_fseForced = false;
                        g_vbsDisabled = false; g_irqPinned = false;
                        SavePersistentState();

                        OptiMax::RevertOptimizations();
                        RevertIsolationPriorities();

                        if (hadCores)    OptiMax::AdvancedPower::RestoreProcessorPowerSettings();
                        if (hadBoot)     OptiMax::Bootloader::RestoreBootTimers();
                        if (hadServices) { OptiMax::Debloat::RestoreTelemetry(); OptiMax::Debloat::RestoreBloatServices(); }
                        if (hadMsi)      OptiMax::V3::RestoreMSIMode();
                        if (hadNet)      OptiMax::V3::RestoreNagleAlgorithm();
                        if (hadFse)      OptiMax::V3::RestoreFSE();
                        if (hadVbs)      OptiMax::V3::RestoreVBS();
                        if (hadIrq)      OptiMax::V3::RestoreGpuIrq();

                        SafeAddLog(needsRestart
                            ? "SYSTEM: All tweaks turned off. Restart your PC so the VBS, MSI mode, GPU interrupt and boot timer changes take effect."
                            : "SYSTEM: All tweaks turned off.");
                    }
                    ImGui::EndChild();

                    if (ImGui::BeginPopupModal("Throttle Background Apps?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "This affects every background app, not just games.");
                        ImGui::Separator(); ImGui::Spacing();
                        ImGui::TextWrapped("Background apps will be set to Below Normal CPU priority, Very Low disk I/O priority, and lowest memory page priority. Windows components and anti-cheat processes are left alone.");
                        ImGui::TextWrapped("Downloads, backups, streaming software, and file transfers may run noticeably slower while this is on.");
                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                        if (PremiumButton("Start Throttling", ImVec2(200 * dpiScale, globalBtnHeight))) {
                            OptiMax::EnableExtremeMode(localAntiCheatActive);
                            SavePersistentState();
                            SafeAddLog("SYSTEM: Background app throttling turned on.");
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(100 * dpiScale, globalBtnHeight))) {
                            g_isolationActive = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    if (!isAdmin) { ImGui::EndDisabled(); }
                }

                else if (activeTab == 8) {
                    ImGui::TextWrapped("Games are added automatically the first time you launch them fullscreen or borderless-fullscreen - no setup needed. Use Browse below only if you want to pre-add a game before its first launch, or one that only ever runs in windowed mode and won't be auto-detected.");
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    std::string selectedExe = "";
                    if (PremiumButton("Browse for Game (.exe)", ImVec2(250.0f * dpiScale, globalBtnHeight * 1.5f))) {
                        selectedExe = OptiMax::FileDialog::SelectExecutable(hwnd);
                    }

                    if (!selectedExe.empty()) {
                        bool isDuplicate = false;
                        {
                            std::lock_guard<std::mutex> lock(g_configMutex);
                            if (std::find(g_targetGames.begin(), g_targetGames.end(), selectedExe) != g_targetGames.end()) {
                                isDuplicate = true;
                            }
                            else {
                                g_targetGames.push_back(selectedExe);
                            }
                        }

                        if (isDuplicate) { SafeAddLog(std::string("LIBRARY: ") + selectedExe + " is already in your library."); }
                        else { SavePersistentState(); SafeAddLog(std::string("LIBRARY: Added ") + selectedExe); }
                    }

                    ImGui::Spacing(); ImGui::Spacing();
                    ImGui::BeginChild("ListArea", ImVec2(0, 0), true);

                    // THE FIX (1 of 2): a deadlock. SavePersistentState() takes
                    // g_configMutex itself, and it used to be called from inside the
                    // P-Cores dropdown handler below - which runs while this very
                    // scope is already holding that same lock. std::mutex is not
                    // recursive, so changing the P-Cores setting on any game in the
                    // library hung the UI thread outright. Nothing about it is
                    // intermittent; it happens every time. The save is deferred to
                    // after the lock is released.
                    //
                    // THE FIX (2 of 2): removal was recorded as an INDEX here and then
                    // used to subscript g_targetGames under a second, separate lock.
                    // The telemetry worker adds auto-detected games and removes
                    // self-healed ones on its own thread every second, so between the
                    // two locks that index could be stale - subscripting out of range
                    // and erasing at an invalid iterator. Removal is by name now, so a
                    // concurrent edit can only make it a no-op.
                    std::string nameToRemove;
                    bool libraryNeedsSave = false;
                    {
                        std::lock_guard<std::mutex> lock(g_configMutex);
                        if (g_targetGames.empty()) { ImGui::TextDisabled("Your library is empty. Games get added here automatically the first time you play them fullscreen."); }
                        else {
                            for (size_t i = 0; i < g_targetGames.size(); ++i) {
                                ImGui::PushID((int)i);
                                ImGui::AlignTextToFramePadding();
                                ImGui::Text(" %s", g_targetGames[i].c_str());

                                // THE FEATURE: per-game P-core lock override. "Default" defers
                                // to the global toggle in the Isolation tab; Lock/Don't Lock
                                // pin this specific game's behavior regardless of that setting -
                                // useful since not every game benefits from P-core affinity
                                // restriction (heavily multi-threaded engines can actually lose
                                // performance from it).
                                ImGui::SameLine(ImGui::GetWindowWidth() - (330.0f * dpiScale));
                                int overrideState = 0; // 0 = default, 1 = lock, 2 = don't lock
                                auto it = g_gamePCoreOverrides.find(g_targetGames[i]);
                                if (it != g_gamePCoreOverrides.end()) overrideState = it->second ? 1 : 2;

                                ImGui::SetNextItemWidth(190.0f * dpiScale);
                                const char* overrideLabels[] = { "P-Cores: Default", "P-Cores: Lock", "P-Cores: Don't Lock" };
                                if (ImGui::Combo("##pcoreoverride", &overrideState, overrideLabels, 3)) {
                                    if (overrideState == 0) g_gamePCoreOverrides.erase(g_targetGames[i]);
                                    else g_gamePCoreOverrides[g_targetGames[i]] = (overrideState == 1);
                                    libraryNeedsSave = true;   // saved after this lock is released
                                }

                                ImGui::SameLine(ImGui::GetWindowWidth() - (120.0f * dpiScale));
                                if (ImGui::Button("Remove", ImVec2(80.0f * dpiScale, 0))) { nameToRemove = g_targetGames[i]; }
                                ImGui::PopID();
                                ImGui::Separator();
                            }
                        }
                    }

                    if (!nameToRemove.empty()) {
                        bool removed = false;
                        {
                            std::lock_guard<std::mutex> lock(g_configMutex);
                            auto it = std::find(g_targetGames.begin(), g_targetGames.end(), nameToRemove);
                            if (it != g_targetGames.end()) {
                                g_targetGames.erase(it);
                                removed = true;
                            }
                            g_gamePCoreOverrides.erase(nameToRemove);
                            // Clear this game's per-game flags too, so removing and later
                            // re-adding a game doesn't silently resurrect flags the user
                            // thought they had thrown away.
                            g_gameDisableSMT.erase(nameToRemove);
                            g_gameHighPriority.erase(nameToRemove);
                            g_gameAggressiveRAMPurge.erase(nameToRemove);
                            g_gameDisableFSO.erase(nameToRemove);
                        }
                        if (removed) {
                            SafeAddLog(std::string("LIBRARY: Removed ") + nameToRemove);
                        }
                        libraryNeedsSave = true;
                    }

                    if (libraryNeedsSave) SavePersistentState();

                    ImGui::EndChild();
                }

                else if (activeTab == 9) {
                    // THE FIX (pre-empted): AlwaysVerticalScrollbar, deliberately NOT
                    // staticCardFlags. staticCardFlags disables scrolling entirely, which
                    // is fine for the fixed-height dashboard cards but wrong for anything
                    // that grows - the Settings tab already shipped that exact bug, where
                    // content past the bottom edge became unreachable on a non-maximized
                    // window. This table grows with the size of the community database, so
                    // it must be able to scroll.
                    ImGui::BeginChild("CloudSyncBox", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

                    ImGui::TextWrapped("These are community-maintained suggestions for which per-game tweaks help which titles. "
                        "Nothing on this page is applied to your system, and nothing is added to your game library automatically. "
                        "A profile only does anything after you click \"Add to My Library\", which copies its settings into your own "
                        "library where you can change or remove them like any other entry.");
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    bool syncing = OptiMax::CloudSync::g_cloudSyncInProgress.load();
                    int cachedRuleCount = OptiMax::CloudSync::g_cloudProfileCount.load();
                    std::string lastSync = OptiMax::CloudSync::GetLastSyncTime();

                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("Last updated:");
                    ImGui::SameLine();
                    ImGui::Text("%s", lastSync.c_str());
                    ImGui::SameLine(0, 30.0f * dpiScale);
                    ImGui::TextDisabled("Profiles:");
                    ImGui::SameLine();
                    ImGui::Text("%d", cachedRuleCount);

                    float syncBtnW = 160.0f * dpiScale;
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - syncBtnW);
                    if (syncing) ImGui::BeginDisabled();
                    if (PremiumButton(syncing ? "Syncing..." : "Check for Updates", ImVec2(syncBtnW, globalBtnHeight))) {
                        OptiMax::CloudSync::StartSync();
                    }
                    if (syncing) ImGui::EndDisabled();

                    ImGui::Spacing(); ImGui::Spacing();

                    std::map<std::string, OptiMax::CloudSync::GameProfile> cloudProfiles = OptiMax::CloudSync::GetSnapshot();

                    if (cloudProfiles.empty()) {
                        ImGui::TextDisabled(syncing
                            ? "Downloading profiles..."
                            : "No profiles cached - hit Check for Updates.");
                    }
                    else {
                        std::vector<std::string> localLibrary;
                        { std::lock_guard<std::mutex> lock(g_configMutex); localLibrary = g_targetGames; }

                        // Deferred so the library is never mutated part-way through
                        // drawing a table that reads from it.
                        std::string pendingAdd = "";

                        // Readable On/off indicators rather than raw 1/0 - a user
                        // scanning this table shouldn't have to remember column order to
                        // know what a "1" meant.
                        auto FlagCell = [](bool enabled) {
                            ImGui::AlignTextToFramePadding();
                            if (enabled) ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "On");
                            else ImGui::TextDisabled("off");
                            };

                        ImGuiTableFlags cloudTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp;

                        if (ImGui::BeginTable("CloudProfileTable", 7, cloudTableFlags)) {
                            ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthStretch, 0.24f);
                            ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_WidthStretch, 0.14f);
                            ImGui::TableSetupColumn("1 Thread/Core", ImGuiTableColumnFlags_WidthStretch, 0.11f);
                            ImGui::TableSetupColumn("High Priority", ImGuiTableColumnFlags_WidthStretch, 0.10f);
                            ImGui::TableSetupColumn("Frequent RAM Clean", ImGuiTableColumnFlags_WidthStretch, 0.12f);
                            ImGui::TableSetupColumn("No Fullscreen Opt.", ImGuiTableColumnFlags_WidthStretch, 0.11f);
                            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.18f);
                            ImGui::TableHeadersRow();

                            for (const auto& entry : cloudProfiles) {
                                const OptiMax::CloudSync::GameProfile& p = entry.second;

                                ImGui::TableNextRow();
                                ImGui::PushID(p.exeName.c_str());

                                ImGui::TableSetColumnIndex(0);
                                ImGui::AlignTextToFramePadding();
                                ImGui::TextUnformatted(p.exeName.c_str());

                                ImGui::TableSetColumnIndex(1);
                                ImGui::AlignTextToFramePadding();
                                if (p.engineType.empty()) ImGui::TextDisabled("Unknown");
                                else ImGui::TextUnformatted(p.engineType.c_str());

                                ImGui::TableSetColumnIndex(2); FlagCell(p.disableSMT);
                                ImGui::TableSetColumnIndex(3); FlagCell(p.highPriority);
                                ImGui::TableSetColumnIndex(4); FlagCell(p.aggressiveRAMPurge);
                                ImGui::TableSetColumnIndex(5); FlagCell(p.disableFSO);

                                ImGui::TableSetColumnIndex(6);
                                bool alreadyInLibrary = std::find(localLibrary.begin(), localLibrary.end(), p.exeName) != localLibrary.end();
                                if (alreadyInLibrary) {
                                    ImGui::BeginDisabled();
                                    PremiumButton("In Library", ImVec2(-1, globalBtnHeight));
                                    ImGui::EndDisabled();
                                }
                                else {
                                    if (PremiumButton("Add to My Library", ImVec2(-1, globalBtnHeight))) {
                                        pendingAdd = p.exeName;
                                    }
                                }

                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }

                        // THE OPT-IN GATE: this is the only place a community profile can
                        // ever affect anything, and it only runs because the user clicked
                        // the button on that specific row.
                        if (!pendingAdd.empty()) {
                            auto found = cloudProfiles.find(pendingAdd);
                            if (found != cloudProfiles.end()) {
                                const OptiMax::CloudSync::GameProfile& p = found->second;
                                {
                                    std::lock_guard<std::mutex> lock(g_configMutex);
                                    if (std::find(g_targetGames.begin(), g_targetGames.end(), p.exeName) == g_targetGames.end()) {
                                        g_targetGames.push_back(p.exeName);
                                    }
                                    g_gameDisableSMT[p.exeName] = p.disableSMT;
                                    g_gameHighPriority[p.exeName] = p.highPriority;
                                    g_gameAggressiveRAMPurge[p.exeName] = p.aggressiveRAMPurge;
                                    g_gameDisableFSO[p.exeName] = p.disableFSO;
                                }
                                // Both of these have to be outside the lock above:
                                // SavePersistentState() takes g_configMutex itself.
                                SavePersistentState();
                                SafeAddLog("LIBRARY: Added " + p.exeName + " from community profiles. Its tweaks apply the next time it launches.");
                            }
                        }
                    }

                    ImGui::EndChild();
                }

                else if (activeTab == 1) {
                    if (ImGui::BeginTable("PowerGrid", 2, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("PowerBox", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "CPU Core Parking"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Stops Windows from idling CPU cores, so no core has to wake up before it can take work. Slightly higher idle power draw.");

                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempCores = g_coresUnparked.load();
                        ImGui::PushID("toggle_park");
                        bool parkToggled = PremiumToggle("Keep All Cores Awake", &tempCores);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_park");
                        bool parkButtoned = PremiumButton(tempCores ? "Allow Core Parking" : "Keep All Cores Awake", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();

                        if (parkToggled || parkButtoned) {
                            if (parkButtoned) tempCores = !tempCores;
                            g_coresUnparked = tempCores; SavePersistentState();
                            if (tempCores) { OptiMax::AdvancedPower::DisableCoreParking(); OptiMax::AdvancedPower::SetEPPToPerformance(); SafeAddLog("POWER: Core parking disabled - all cores stay awake."); }
                            else {
                                // THE FIX: this branch called Power::RestorePowerPlan()
                                // and logged "Core parking restored to the Windows
                                // default". RestorePowerPlan switches the active power
                                // SCHEME back to whatever it was before Core Tweaks - it
                                // has nothing to do with the core parking setting, and
                                // when Core Tweaks were never engaged it does nothing at
                                // all. So core parking and the energy performance
                                // preference stayed forced on both AC and battery while
                                // the log said they had been put back, and worse, if Core
                                // Tweaks WERE active this silently dropped the user out
                                // of the performance power plan as a side effect of an
                                // unrelated toggle.
                                OptiMax::AdvancedPower::RestoreProcessorPowerSettings();
                            }
                        }
                        ImGui::EndChild();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("BootloaderBox", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Boot Timer Settings"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Turns off the HPET timer and dynamic tick in the boot configuration, so Windows uses the CPU's own clock instead. Takes effect after a restart.");

                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempBoot = g_bootOptimized.load();
                        ImGui::PushID("toggle_boot");
                        bool bootToggled = PremiumToggle("Use CPU Clock for Timing", &tempBoot);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_boot");
                        // Label follows the current state, as every other card here does.
                        // A fixed "Apply" caption on a button that now also reverts told
                        // the user the opposite of what pressing it would do.
                        bool bootButtoned = PremiumButton(tempBoot ? "Restore Default Boot Timers (needs restart)"
                                                                   : "Apply Boot Timer Settings (needs restart)",
                                                          ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();

                        if (bootToggled || bootButtoned) {
                            if (bootButtoned) tempBoot = !tempBoot;
                            g_bootOptimized = tempBoot; SavePersistentState();
                            if (tempBoot) { RestorePointBeforeTweak(); OptiMax::Bootloader::OptimizeTimers(); SafeAddLog("BOOT: Boot timer settings applied. Restart to take effect."); }
                            else { OptiMax::Bootloader::RestoreBootTimers(); }
                        }
                        ImGui::EndChild();
                        ImGui::EndTable();
                    }
                }

                else if (activeTab == 2) {
                    if (ImGui::BeginTable("DebloatGrid", 2, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("DebloatBox", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Telemetry & Background Services"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Disables telemetry, SysMain, Windows Search, and WAP push routing. Search indexing and app preloading stop working until you restore them.");

                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempSan = g_servicesSanitized.load();
                        ImGui::PushID("toggle_san");
                        bool sanitizeToggled = PremiumToggle("Disable These Services", &tempSan);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_san");
                        bool sanitizeButtoned = PremiumButton(tempSan ? "Restore Services" : "Disable Services", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();

                        if (sanitizeToggled || sanitizeButtoned) {
                            if (sanitizeButtoned) tempSan = !tempSan;
                            g_servicesSanitized = tempSan; SavePersistentState();
                            if (tempSan) { RestorePointBeforeTweak(); OptiMax::Debloat::DisableTelemetry(); OptiMax::Debloat::StopBloatServices(); SafeAddLog("SERVICES: Telemetry and background services disabled."); }
                            else { OptiMax::Debloat::RestoreTelemetry(); OptiMax::Debloat::RestoreBloatServices(); SafeAddLog("SERVICES: Services restored to their original startup types."); }
                        }
                        ImGui::EndChild();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("PurgerBox", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Cache Cleanup"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Deletes GPU shader caches and temp files, and flushes the DNS cache. Games rebuild their shader cache next launch, so expect one slower session after.");
                        ImGui::SetCursorPosY(bottomAnchorY);
                        if (PremiumButton("Clear Caches & Temp Files", ImVec2(-1, globalBtnHeight))) { OptiMax::Purger::DeepClean(); }
                        ImGui::EndChild();
                        ImGui::EndTable();
                    }
                }

                else if (activeTab == 3) {
                    ImGui::TextWrapped("Average FPS doesn't tell you about stutter. This 60-second test repeatedly polls a high-resolution timer and records the longest gaps, which is a good proxy for the driver interrupt delays behind frame time spikes.");
                    ImGui::Spacing();
                    if (localBenchmarking) {
                        float progress = (float)(currentTime - g_benchStartTime) / 60000.0f;
                        ImGui::TextColored(sexyPurple, "Measuring... Please do not move your mouse or start other programs.");
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, sexyPurple); ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f)); ImGui::PopStyleColor();
                    }
                    else {
                        if (PremiumButton("Run 60-Second Latency Test", ImVec2(-1, globalBtnHeight))) {
                            SafeAddLog("LATENCY TEST: Started (60 seconds).");
                            g_isBenchmarking = true; g_benchStartTime = GetTickCount64();
                            if (g_benchThread.joinable()) { g_benchThread.join(); }
                            g_benchThread = std::thread([]() {
                                // Guarded for the same reason as the other workers: an
                                // exception reaching a thread's entry point terminates the
                                // process. It also owns g_isBenchmarking, so without this
                                // a throw part-way through would have left the UI showing
                                // "Measuring..." with no way back.
                                try {
                                    for (int i = 0; i < 500; ++i) { std::this_thread::yield(); }
                                    std::vector<double> localSamples; localSamples.reserve(100000);
                                    ULONGLONG start = GetTickCount64();
                                    while (GetTickCount64() - start < 60000 && g_isAppRunning) { localSamples.push_back(OptiMax::Profiler::GetMaxDPCSpikeMicroseconds()); }
                                    auto audit = OptiMax::Benchmark::CalculateAudit(localSamples);
                                    std::lock_guard<std::mutex> lock(g_benchMutex); g_lastAudit = audit; g_isBenchmarking = false;
                                    SafeAddLog("LATENCY TEST: Complete.");
                                }
                                catch (...) {
                                    g_isBenchmarking = false;
                                    SafeAddLog("LATENCY TEST: Stopped - the measurement failed unexpectedly.");
                                }
                                });
                        }
                    }
                    ImGui::Spacing();

                    OptiMax::Benchmark::AuditResult displayAudit;
                    { std::lock_guard<std::mutex> lock(g_benchMutex); displayAudit = g_lastAudit; }

                    if (displayAudit.averageDPC > 0.0 && !localBenchmarking) {
                        ImGui::BeginChild("AuditResults", ImVec2(0, 0), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Test Complete."); if (m_fontBold) ImGui::PopFont();
                        ImGui::Separator(); ImGui::Spacing();
                        if (ImGui::BeginTable("AuditStats", 2, ImGuiTableFlags_SizingStretchProp)) {
                            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.45f); ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextWrapped("Average timer gap:"); ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%.2f us", displayAudit.averageDPC);
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextWrapped("Longest single gap:"); ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%.2f us", displayAudit.maxDPC);
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextWrapped("Worst 1%% of gaps:"); ImGui::TableSetColumnIndex(1);
                            ImVec4 rankColor = (displayAudit.onePercentHighDPC < 500) ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f) : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
                            if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(rankColor, "%.2f us", displayAudit.onePercentHighDPC); if (m_fontBold) ImGui::PopFont();
                            ImGui::EndTable();
                        }
                        ImGui::Spacing();
                        if (displayAudit.onePercentHighDPC < 500) ImGui::TextDisabled("Result: Good. Timing is consistent - unlikely to be a source of stutter.");
                        else ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Result: Inconsistent. Try the Services & Cleanup and Power & Timers tabs, and check for outdated drivers.");
                        ImGui::EndChild();
                    }
                }

                else if (activeTab == 4) {
                    if (PremiumToggle("Show Live Readings", &isProfilerRunning)) {
                        SafeAddLog(isProfilerRunning ? "MONITOR: Live readings started." : "MONITOR: Live readings stopped.");
                    }
                    ImGui::Spacing();
                    ImGui::BeginChild("TelemetryGauges", ImVec2(0, 0), true, staticCardFlags);
                    ImGui::Spacing();
                    float gaugeRadius = 45.0f * dpiScale;
                    ImGui::Columns(5, "GaugeColumns", false);

                    char cpuText[16]; sprintf_s(cpuText, "%d%%", cachedCpuLoad); PremiumGauge("CPU Load", cachedCpuLoad / 100.0f, cpuText, gaugeRadius); ImGui::NextColumn();
                    char gpuText[16]; sprintf_s(gpuText, "%d%%", cachedGpuLoad); PremiumGauge("GPU Load", cachedGpuLoad / 100.0f, gpuText, gaugeRadius); ImGui::NextColumn();
                    char ramText[16]; sprintf_s(ramText, "%d%%", cachedRam); PremiumGauge("RAM Usage", cachedRam / 100.0f, ramText, gaugeRadius); ImGui::NextColumn();
                    char dpcText[16]; sprintf_s(dpcText, "%.0f us", cachedDpcSpike); float dpcFraction = ImClamp((float)cachedDpcSpike / 2000.0f, 0.0f, 1.0f); PremiumGauge("DPC Latency", dpcFraction, dpcText, gaugeRadius); ImGui::NextColumn();
                    char timerText[16]; sprintf_s(timerText, "%.2f ms", cachedTimer); float timerFraction = (cachedTimer <= 0.5f && cachedTimer > 0.0f) ? 1.0f : 0.2f; PremiumGauge("System Timer", timerFraction, timerText, gaugeRadius);
                    ImGui::Columns(1); ImGui::EndChild();
                }

                else if (activeTab == 5) {
                    // THE FIX: staticCardFlags disables scrolling (shared by every card
                    // in the app, most of which are fixed-height and don't need it).
                    // Settings has grown enough - RAM cleaner section, Profile export/
                    // import, Danger Zone - that its content can exceed the window
                    // height when not maximized, with no way to reach what's cut off.
                    // This is the only BeginChild changed; every other card keeps the
                    // shared no-scroll flags untouched.
                    ImGui::BeginChild("SettingsBox", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

                    bool t1 = g_settingAutoStart.load();
                    if (PremiumToggle("Start OptiMax with Windows (minimized to tray)", &t1)) {
                        g_settingAutoStart = t1; SavePersistentState();
                        SetStartupRegistry(t1);
                        SafeAddLog(t1 ? "SETTINGS: Start with Windows enabled." : "SETTINGS: Start with Windows disabled.");
                    }
                    ImGui::Spacing(); ImGui::Spacing();

                    // Reflects whether "Minimize to Tray" was remembered from the close
                    // dialog (g_closeBehavior == 1). Toggling this off resets it back to
                    // asking every time; toggling it on sets the same remembered behavior
                    // the dialog itself sets, without needing to close the app first.
                    bool tCloseTray = (g_closeBehavior.load() == 1);
                    if (PremiumToggle("Always Minimize to Tray on Close", &tCloseTray)) {
                        g_closeBehavior = tCloseTray ? 1 : 0;
                        SavePersistentState();
                        SafeAddLog(tCloseTray
                            ? "SETTINGS: Closing will now always minimize to tray. Use the tray icon's right-click menu to Exit."
                            : "SETTINGS: Closing will ask each time again.");
                    }
                    ImGui::Spacing(); ImGui::Spacing();

                    bool t2 = g_settingSafeMode.load();
                    if (PremiumToggle("Create a System Restore Point Before Applying Tweaks", &t2)) {
                        g_settingSafeMode = t2; SavePersistentState();
                        SafeAddLog(t2 ? "SETTINGS: Restore points enabled." : "SETTINGS: Restore points disabled.");
                    }
                    ImGui::Spacing(); ImGui::Spacing();

                    bool t3 = g_settingMpoDisable.load();
                    if (PremiumToggle("Disable Multi-Plane Overlay (fixes some flickering)", &t3)) {
                        // The registry write is what makes this real; the saved
                        // preference only ever recorded the switch position.
                        // Ordered so the setting is not remembered as applied if
                        // applying it failed.
                        if (OptiMax::Profiler::SetMPODisabled(t3)) {
                            g_settingMpoDisable = t3; SavePersistentState();
                            SafeAddLog(t3
                                ? "SETTINGS: Multi-Plane Overlay disabled - restart Windows for it to take effect."
                                : "SETTINGS: Multi-Plane Overlay restored - restart Windows for it to take effect.");
                        }
                        else {
                            SafeAddLog("SETTINGS WARNING: Could not change the Multi-Plane Overlay setting - "
                                "this needs administrator rights.");
                        }
                    }
                    ImGui::Spacing(); ImGui::Spacing();

                    bool t4 = g_settingAutoRam.load();
                    char autoRamLabel[64];
                    sprintf_s(autoRamLabel, "Clean RAM Automatically Above %d%% Usage", g_ramCleanThreshold.load());
                    // THE FIX: Dashboard is the master control for enabling/disabling this
                    // feature - this is a read-only mirror of its current state so there's
                    // no ambiguity about which toggle is "in charge." The threshold slider
                    // below is unaffected and stays fully interactive, since it's a setting
                    // that only lives here.
                    ImGui::BeginDisabled();
                    PremiumToggle(autoRamLabel, &t4);
                    ImGui::EndDisabled();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Turn this on or off from the Dashboard");

                    if (!t4) ImGui::BeginDisabled();
                    ImGui::Indent();
                    ImGui::SetNextItemWidth(220);
                    int thresholdEdit = g_ramCleanThreshold.load();
                    // Clamp 50-95: below 50% you'd be purging almost constantly (disk thrashing on HDDs),
                    // above 95% the purge barely has any headroom left to reclaim before you're already maxed out.
                    if (ImGui::SliderInt("Clean above this RAM usage", &thresholdEdit, 50, 95, "%d%%")) {
                        g_ramCleanThreshold = thresholdEdit;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        SavePersistentState();
                        SafeAddLog("SETTINGS: RAM cleaning threshold set to " + std::to_string(g_ramCleanThreshold.load()) + "%.");
                    }
                    ImGui::Unindent();
                    if (!t4) ImGui::EndDisabled();

                    ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); ImGui::Spacing();
                    ImGui::TextColored(sexyPurple, "Backup & Transfer");
                    ImGui::TextWrapped("Save your settings and game library to a file, or load one from another PC.");
                    ImGui::Spacing();
                    {
                        float pAvailW = ImGui::GetContentRegionAvail().x;
                        float pBtnW = (pAvailW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

                        if (PremiumButton("Save to File", ImVec2(pBtnW, globalBtnHeight))) {
                            std::wstring savePath = OptiMax::FileDialog::SaveProfileFile(hwnd);
                            if (!savePath.empty()) {
                                if (!ExportProfile(savePath)) SafeAddLog("BACKUP ERROR: Could not write to that location.");
                            }
                        }
                        ImGui::SameLine();
                        if (PremiumButton("Load from File", ImVec2(pBtnW, globalBtnHeight))) {
                            std::wstring openPath = OptiMax::FileDialog::OpenProfileFile(hwnd);
                            if (!openPath.empty()) {
                                ImportProfile(openPath);
                            }
                        }
                    }

                    ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Undo Everything");
                    ImGui::TextWrapped("Reverts every registry and service change OptiMax has made - VBS and Memory Integrity, MSI mode, GPU interrupt pinning, exclusive fullscreen, telemetry, and the disabled background services - back to the Windows defaults. Unlike closing the app, this also undoes the changes that don't revert on their own.");
                    ImGui::Spacing();
                    if (PremiumButton("Restore Windows Defaults", ImVec2(-1, globalBtnHeight))) {
                        ImGui::OpenPopup("Restore Windows Defaults?");
                    }

                    if (ImGui::BeginPopupModal("Restore Windows Defaults?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "This undoes every change OptiMax has made.");
                        ImGui::TextWrapped("VBS and Memory Integrity, MSI mode, GPU interrupt pinning, exclusive fullscreen, telemetry, and the disabled background services all go back to their Windows defaults. Restart afterward so everything takes effect.");
                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                        float rAvailW = ImGui::GetContentRegionAvail().x;
                        float rBtnW = (rAvailW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

                        if (PremiumButton("Restore Defaults", ImVec2(rBtnW, globalBtnHeight))) {
                            // THE FIX: this called RestoreAllToWindowsDefaults() and
                            // nothing else. The system changes were undone, but not one
                            // of the persisted toggle flags was cleared and
                            // SavePersistentState() was never called - so HKCU still
                            // recorded MSIEnabled, VbsDisabled, IrqPinned,
                            // NetworkTweaked, ServicesSanitized and the rest as ON. The
                            // boot-time re-apply block in wWinMain reads exactly those
                            // values and re-applies every single tweak, which means the
                            // one action a user takes to walk everything back was
                            // silently reversed at their next launch - and every toggle
                            // in the UI still showed as on in the meantime. Undoing the
                            // changes and forgetting they were ever wanted have to
                            // happen together.
                            g_isCoreActive = false;      g_autoBoostEnabled = false;
                            g_coresUnparked = false;     g_bootOptimized = false;
                            g_servicesSanitized = false; g_privacyTweaked = false;
                            g_extendedServices = false;  g_telemetryTasks = false;
                            g_snappyUI = false;          g_mouseAccelDisabled = false;
                            g_gameDvrDisabled = false;   g_mmcssGames = false;
                            g_globalTimerRes = false;    g_isolationActive = false;
                            g_pCoresLocked = false;      g_msiEnabled = false;
                            g_networkTweaked = false;    g_fseForced = false;
                            g_vbsDisabled = false;       g_irqPinned = false;
                            g_settingMpoDisable = false;

                            OptiMax::RestoreAllToWindowsDefaults();

                            // Background app throttling lives on the App side, so
                            // RestoreAllToWindowsDefaults() cannot reach it - without
                            // this, every background process stayed at Below Normal
                            // priority with Very Low disk and memory priority.
                            RevertIsolationPriorities();

                            SavePersistentState();
                            SafeAddLog("SYSTEM: All OptiMax settings cleared - nothing will be re-applied at the next launch.");
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(rBtnW, globalBtnHeight))) {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::EndChild();
                }

                else if (activeTab == 6) {
                    ImGui::BeginChild("LogArea", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                    std::lock_guard<std::mutex> lock(g_logMutex);
                    for (auto& l : g_logs) { ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%s", l.c_str()); }
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) { ImGui::SetScrollHereY(1.0f); }
                    ImGui::EndChild();
                }

                else if (activeTab == 7) {
                    ImGui::TextWrapped("These change driver and boot-level Windows settings. Most need a restart, some can affect stability on specific hardware, and one of them turns off security features. Read each description before enabling it.");
                    ImGui::Spacing();

                    if (ImGui::BeginTable("ExperimentalGrid", 2, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("ExpBox1", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "P-Core Affinity Lock"); if (m_fontBold) ImGui::PopFont();

                        if (isHeterogeneousCPU) {
                            ImGui::TextWrapped("Restricts detected games to the CPU's performance cores. Not every game benefits, so you can override it per game in My Games.");
                        }
                        else {
                            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Not applicable to this CPU");
                            ImGui::TextWrapped("This CPU doesn't have separate performance and efficiency cores.");
                        }

                        // THE FIX: the else branch above used to fall through without
                        // setting the anchor at all, so on a non-hybrid CPU the toggle
                        // landed wherever the text happened to end instead of lining up
                        // with every other card. Anchored unconditionally now.
                        ImGui::SetCursorPosY(toggleAnchorY);

                        if (!isHeterogeneousCPU) ImGui::BeginDisabled();

                        bool tempPcores = g_pCoresLocked.load();
                        ImGui::PushID("toggle_pcore");
                        bool pcoreToggled = PremiumToggle("Lock Games to P-Cores", &tempPcores);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_pcore");
                        bool pcoreButtoned = PremiumButton(tempPcores ? "Remove P-Core Lock" : "Lock Games to P-Cores", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();

                        if (pcoreToggled || pcoreButtoned) {
                            if (pcoreButtoned) tempPcores = !tempPcores;
                            g_pCoresLocked = tempPcores; SavePersistentState();
                            SafeAddLog(tempPcores ? "TWEAK: P-core lock on - applied automatically when a game starts." : "TWEAK: P-core lock off.");
                        }

                        if (!isHeterogeneousCPU) ImGui::EndDisabled();
                        ImGui::EndChild();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("ExpBox2", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "GPU MSI Mode"); if (m_fontBold) ImGui::PopFont();
                        // Accuracy note: most modern GPUs already default to MSI-X, so this
                        // is frequently a no-op. Say so rather than implying a guaranteed win.
                        ImGui::TextWrapped("Switches the GPU to message-signalled interrupts, if not already enabled. Most modern GPUs default to this, so it often changes nothing. Needs a restart.");

                        ImGui::SetCursorPosY(toggleAnchorY);

                        bool tempMsi = g_msiEnabled.load();
                        ImGui::PushID("toggle_msi");
                        bool msiToggled = PremiumToggle("Enable MSI Mode", &tempMsi);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_msi");
                        bool msiButtoned = PremiumButton(tempMsi ? "Disable MSI Mode" : "Enable MSI Mode (needs restart)", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();

                        if (msiToggled || msiButtoned) {
                            if (msiButtoned) tempMsi = !tempMsi;
                            g_msiEnabled = tempMsi; SavePersistentState();

                            if (tempMsi) {
                                RestorePointBeforeTweak();
                                OptiMax::V3::InjectMSIMode();
                                // THE FIX: OptiMaxVerifier was included but never called from
                                // anywhere in the program, so nothing ever confirmed that a tweak
                                // had actually been applied. InjectMSIMode() ignores every
                                // RegSetValueExW return value, so a denied write still produced a
                                // success message. Read the registry back and report what is
                                // really there instead of what we intended to write.
                                if (OptiMaxVerifier::CheckMSIMode())
                                    SafeAddLog("TWEAK: GPU MSI mode enabled and confirmed in the registry. Restart to take effect.");
                                else
                                    SafeAddLog("TWEAK FAILED: GPU MSI mode was not written - the registry still shows it off. Administrator rights are required.");
                            }
                            else {
                                // THE FIX: this branch logged "setting removed" and removed
                                // nothing. RestoreMSIMode() existed the whole time but was
                                // only reachable from Restore All Defaults, so turning the
                                // toggle off left MSISupported written to the device while
                                // the UI showed it as reverted.
                                OptiMax::V3::RestoreMSIMode();
                                if (!OptiMaxVerifier::CheckMSIMode())
                                    SafeAddLog("TWEAK: GPU MSI mode setting removed and confirmed gone. Restart to return to the driver default.");
                                else
                                    SafeAddLog("TWEAK FAILED: the GPU MSI mode override is still present after the restore attempt. Administrator rights are required.");
                            }
                        }
                        ImGui::EndChild();

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("ExpBox3", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "TCP Latency Tweaks"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Sends small packets immediately instead of batching them (disables Nagle and delayed ACKs). Helps online latency, slightly worse for bulk transfers.");

                        ImGui::SetCursorPosY(toggleAnchorY);

                        bool tempNet = g_networkTweaked.load();
                        ImGui::PushID("toggle_net");
                        bool netToggled = PremiumToggle("Send Small Packets Immediately", &tempNet);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_net");
                        bool netButtoned = PremiumButton(tempNet ? "Restore Default TCP Behavior" : "Apply TCP Latency Tweaks", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();

                        if (netToggled || netButtoned) {
                            if (netButtoned) tempNet = !tempNet;
                            g_networkTweaked = tempNet; SavePersistentState();

                            if (tempNet) {
                                OptiMax::V3::BypassNagleAlgorithm();
                                // Same reason as the MSI branch above: BypassNagleAlgorithm()
                                // discards every write result, so read the values back.
                                if (OptiMaxVerifier::CheckNetworkTweaks())
                                    SafeAddLog("TWEAK: TCP latency tweaks applied to active network adapters and confirmed in the registry.");
                                else
                                    SafeAddLog("TWEAK FAILED: no adapter shows the TCP latency values - the writes did not land, or no Ethernet/Wi-Fi adapter is currently connected.");
                            }
                            else {
                                OptiMax::V3::RestoreNagleAlgorithm();
                                if (!OptiMaxVerifier::CheckNetworkTweaks())
                                    SafeAddLog("TWEAK: TCP latency tweaks removed and confirmed gone. Restart to return to default TCP timing.");
                                else
                                    SafeAddLog("TWEAK FAILED: TCP latency overrides are still present on at least one adapter after the restore attempt.");
                            }
                        }
                        ImGui::EndChild();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("ExpBox4", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Exclusive Fullscreen"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Lets fullscreen games take the display directly instead of going through the compositor. Alt-Tab gets slower. Applies to all games, not just one.");

                        ImGui::SetCursorPosY(toggleAnchorY);

                        bool tempFse = g_fseForced.load();
                        ImGui::PushID("toggle_fse");
                        bool fseToggled = PremiumToggle("Prefer Exclusive Fullscreen", &tempFse);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_fse");
                        bool fseButtoned = PremiumButton(tempFse ? "Restore Fullscreen Optimizations" : "Prefer Exclusive Fullscreen", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();

                        if (fseToggled || fseButtoned) {
                            if (fseButtoned) tempFse = !tempFse;
                            g_fseForced = tempFse; SavePersistentState();

                            if (tempFse) {
                                // Applied by the FSE watchdog in the telemetry worker,
                                // which re-asserts the keys whenever g_fseForced is set -
                                // that is the whole point of this tweak, since Windows
                                // resets these values on its own.
                                SafeAddLog("TWEAK: Exclusive fullscreen preferred. OptiMax will re-apply this if Windows resets it.");
                            }
                            else {
                                // THE FIX: neither branch of this toggle called anything.
                                // Turning it on works only because the watchdog notices
                                // the flag - but turning it off merely stopped the watchdog
                                // re-asserting, leaving GameDVR_FSEBehaviorMode and its two
                                // companions written to the registry permanently while the
                                // log claimed they had been restored. g_fseForced is already
                                // false by this point, so the watchdog will not undo this.
                                OptiMax::V3::RestoreFSE();
                                SafeAddLog("TWEAK: Windows Fullscreen Optimizations restored.");
                            }
                        }
                        ImGui::EndChild();

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("ExpBox5", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold);
                        // Accuracy note: this one is a real, well-documented performance win -
                        // but it removes genuine security features. The trade-off belongs in
                        // the label, not buried in a footnote, so the heading carries it.
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Disable VBS (reduces security)");
                        if (m_fontBold) ImGui::PopFont();
                        // The trade-off stays visible: the heading says "reduces security"
                        // and the body names what is actually lost. Kept to three lines so
                        // it can't overlap the controls, per the card geometry.
                        ImGui::TextWrapped("A real gain of a few percent - Windows stops running under a hypervisor. Costs you Memory Integrity and Credential Guard. Needs a restart.");

                        ImGui::SetCursorPosY(toggleAnchorY);

                        bool tempVbs = g_vbsDisabled.load();
                        ImGui::PushID("toggle_vbs");
                        bool vbsToggled = PremiumToggle("Trade Security for Performance", &tempVbs);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_vbs");
                        bool vbsButtoned = PremiumButton(tempVbs ? "Re-enable VBS & Memory Integrity" : "Disable VBS (needs restart)", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();

                        if (vbsToggled || vbsButtoned) {
                            if (vbsButtoned) tempVbs = !tempVbs;
                            g_vbsDisabled = tempVbs; SavePersistentState();

                            if (tempVbs) {
                                RestorePointBeforeTweak();
                                if (OptiMax::V3::DisableVBS()) {
                                    SafeAddLog("TWEAK: VBS and Memory Integrity disabled - Credential Guard is now off too. Restart to take effect.");
                                }
                                else {
                                    // Ordered so the switch does not stay on after a change
                                    // that did not happen.
                                    g_vbsDisabled = false; SavePersistentState();
                                    SafeAddLog("TWEAK FAILED: VBS could not be disabled - administrator rights are required. Nothing was changed.");
                                }
                            }
                            else {
                                // THE FIX: the most serious of the three dead revert paths.
                                // This claimed Memory Integrity and Credential Guard were
                                // back on while RestoreVBS() - which existed, and worked -
                                // was never called, so the machine stayed unprotected and
                                // the user had been told otherwise. RestoreVBS() now reads
                                // the keys back and logs the real state itself, so a
                                // failure here can no longer read as "you're protected".
                                if (!OptiMax::V3::RestoreVBS()) {
                                    g_vbsDisabled = true; SavePersistentState();
                                }
                            }
                        }
                        ImGui::EndChild();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("ExpBox6", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "GPU Interrupt Affinity"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Pins GPU driver interrupts to one performance core so they don't collide with input and audio. Core picked from real CPU topology. Needs a restart.");

                        ImGui::SetCursorPosY(toggleAnchorY);

                        bool tempIrq = g_irqPinned.load();
                        ImGui::PushID("toggle_irq");
                        bool irqToggled = PremiumToggle("Pin GPU Interrupts", &tempIrq);
                        ImGui::PopID();

                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_irq");
                        bool irqButtoned = PremiumButton(tempIrq ? "Unpin GPU Interrupts" : "Pin GPU Interrupts (needs restart)", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();

                        if (irqToggled || irqButtoned) {
                            if (irqButtoned) tempIrq = !tempIrq;
                            g_irqPinned = tempIrq; SavePersistentState();

                            if (tempIrq) {
                                RestorePointBeforeTweak();
                                OptiMax::V3::PinGpuIrq();
                                SafeAddLog("TWEAK: GPU interrupts pinned to a performance core. Restart to take effect.");
                            }
                            else {
                                // THE FIX: same dead revert path - RestoreGpuIrq() was never
                                // called, so the interrupt affinity policy stayed written to
                                // the device after the user turned the toggle off.
                                OptiMax::V3::RestoreGpuIrq();
                                SafeAddLog("TWEAK: GPU interrupt pinning removed - Windows will place them again.");
                            }
                        }
                        ImGui::EndChild();

                        // --- Mouse Acceleration (biggest real aim/latency win) ---
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("ExpBox7", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Disable Mouse Acceleration"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Turns off 'Enhance Pointer Precision' so aim is 1:1 - the same physical movement always maps to the same in-game turn. The single most useful setting for consistent low-sens aim. Applies instantly.");
                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempMouse = g_mouseAccelDisabled.load();
                        ImGui::PushID("toggle_mouse");
                        bool mouseToggled = PremiumToggle("1:1 Mouse Input", &tempMouse);
                        ImGui::PopID();
                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_mouse");
                        bool mouseButtoned = PremiumButton(tempMouse ? "Restore Mouse Acceleration" : "Disable Mouse Acceleration", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();
                        if (mouseToggled || mouseButtoned) {
                            if (mouseButtoned) tempMouse = !tempMouse;
                            g_mouseAccelDisabled = tempMouse; SavePersistentState();
                            OptiMax::V3::SetMouseAcceleration(tempMouse);
                        }
                        ImGui::EndChild();

                        // --- GameDVR / Game Bar recording ---
                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("ExpBox8", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Disable GameDVR / Game Bar"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Stops Xbox Game Bar's background recording pipeline, which runs even when you're not recording. A real, low-risk frame-time win. No restart needed.");
                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempDvr = g_gameDvrDisabled.load();
                        ImGui::PushID("toggle_dvr");
                        bool dvrToggled = PremiumToggle("Disable Background Recording", &tempDvr);
                        ImGui::PopID();
                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_dvr");
                        bool dvrButtoned = PremiumButton(tempDvr ? "Restore GameDVR" : "Disable GameDVR", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();
                        if (dvrToggled || dvrButtoned) {
                            if (dvrButtoned) tempDvr = !tempDvr;
                            g_gameDvrDisabled = tempDvr; SavePersistentState();
                            OptiMax::V3::SetGameDVR(tempDvr);
                        }
                        ImGui::EndChild();

                        // --- MMCSS Games profile ---
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("ExpBox9", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "MMCSS Games Priority"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Raises the scheduling profile Windows gives games' render threads via the MMCSS 'Games' task (Priority and SFIO Priority). Modest but real, and safe. No restart needed.");
                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempMmcss = g_mmcssGames.load();
                        ImGui::PushID("toggle_mmcss");
                        bool mmcssToggled = PremiumToggle("Boost Game Scheduling", &tempMmcss);
                        ImGui::PopID();
                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_mmcss");
                        bool mmcssButtoned = PremiumButton(tempMmcss ? "Restore MMCSS Defaults" : "Boost Game Scheduling", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();
                        if (mmcssToggled || mmcssButtoned) {
                            if (mmcssButtoned) tempMmcss = !tempMmcss;
                            g_mmcssGames = tempMmcss; SavePersistentState();
                            OptiMax::V3::SetMMCSSGamesProfile(tempMmcss);
                        }
                        ImGui::EndChild();

                        // --- Global Timer Resolution ---
                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("ExpBox10", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Global Timer Resolution"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Makes the low-latency timer resolution OptiMax requests apply system-wide instead of only to the foreground app (Windows 11 22H2+). Complements the timer tweaks. Needs a restart.");
                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempGtr = g_globalTimerRes.load();
                        ImGui::PushID("toggle_gtr");
                        bool gtrToggled = PremiumToggle("Global Timer Requests", &tempGtr);
                        ImGui::PopID();
                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_gtr");
                        bool gtrButtoned = PremiumButton(tempGtr ? "Restore Default (needs restart)" : "Enable Global Timer (needs restart)", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();
                        if (gtrToggled || gtrButtoned) {
                            if (gtrButtoned) tempGtr = !tempGtr;
                            g_globalTimerRes = tempGtr; SavePersistentState();
                            OptiMax::V3::SetGlobalTimerResolution(tempGtr);
                        }
                        ImGui::EndChild();

                        ImGui::EndTable();
                    }
                }

                else if (activeTab == 10) {
                    ImGui::TextWrapped("Debloat tools use native Windows APIs - no PowerShell, no batch scripts, nothing dropped to disk, which keeps antivirus heuristics quiet. Every switch here is reversible: toggle it back off to restore Windows' original behaviour. (The one exception is Windows Health Repair at the bottom, which drives Microsoft's own Dism.exe and sfc.exe directly - there is no API for those.)");
                    ImGui::Spacing();

                    if (ImGui::BeginTable("DebloatPageGrid", 2, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp)) {
                        // --- Privacy & Telemetry ---
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("DbPrivacy", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Privacy & Telemetry"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Turns off the advertising ID, activity history, tailored experiences, feedback prompts, Windows Copilot, and Start-menu suggestions via registry policy.");
                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempPriv = g_privacyTweaked.load();
                        ImGui::PushID("toggle_priv");
                        bool privToggled = PremiumToggle("Disable Tracking & Suggestions", &tempPriv);
                        ImGui::PopID();
                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_priv");
                        bool privButtoned = PremiumButton(tempPriv ? "Restore Privacy Defaults" : "Apply Privacy Tweaks", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();
                        if (privToggled || privButtoned) {
                            if (privButtoned) tempPriv = !tempPriv;
                            g_privacyTweaked = tempPriv; SavePersistentState();
                            OptiMax::Debloat::SetPrivacyTweaks(tempPriv);
                        }
                        ImGui::EndChild();

                        // --- Extended Services ---
                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("DbExtSvc", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Extra Background Services"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Disables lower-value services on top of the Services & Cleanup page: Downloaded Maps, Retail Demo, Fax, Parental Controls, WMP Network Sharing, and Geolocation.");
                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempExt = g_extendedServices.load();
                        ImGui::PushID("toggle_ext");
                        bool extToggled = PremiumToggle("Disable Extra Services", &tempExt);
                        ImGui::PopID();
                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_ext");
                        bool extButtoned = PremiumButton(tempExt ? "Restore These Services" : "Disable Extra Services", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();
                        if (extToggled || extButtoned) {
                            if (extButtoned) tempExt = !tempExt;
                            g_extendedServices = tempExt; SavePersistentState();
                            OptiMax::Debloat::SetExtendedServices(tempExt);
                        }
                        ImGui::EndChild();

                        // --- Telemetry Scheduled Tasks ---
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::BeginChild("DbTasks", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Telemetry Scheduled Tasks"); if (m_fontBold) ImGui::PopFont();
                        ImGui::TextWrapped("Disables the Customer Experience Improvement, Compatibility Appraiser, and disk-diagnostic data-collection tasks through the Task Scheduler API - no schtasks.exe is launched.");
                        ImGui::SetCursorPosY(toggleAnchorY);
                        bool tempTask = g_telemetryTasks.load();
                        ImGui::PushID("toggle_task");
                        bool taskToggled = PremiumToggle("Disable Telemetry Tasks", &tempTask);
                        ImGui::PopID();
                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_task");
                        bool taskButtoned = PremiumButton(tempTask ? "Re-enable These Tasks" : "Disable Telemetry Tasks", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();
                        if (taskToggled || taskButtoned) {
                            if (taskButtoned) tempTask = !tempTask;
                            g_telemetryTasks = tempTask; SavePersistentState();
                            OptiMax::Debloat::SetTelemetryTasks(tempTask);
                        }
                        ImGui::EndChild();

                        // --- Windows 11 Snappy UI (gated on Win11 detection) ---
                        ImGui::TableSetColumnIndex(1);
                        ImGui::BeginChild("DbSnappy", ImVec2(0, cardHeight), true, staticCardFlags);
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Snappier UI (Windows 11)"); if (m_fontBold) ImGui::PopFont();
                        if (isWindows11) {
                            ImGui::TextWrapped("Drops the menu-open delay to 0ms and the mouse-hover delay to 10ms (Windows ships both at 400ms), so menus and tooltips feel instant. Applies live, no restart.");
                        }
                        else {
                            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Windows 11 only");
                            ImGui::TextWrapped("This tweak targets Windows 11's shell delays and is disabled on this OS.");
                        }
                        ImGui::SetCursorPosY(toggleAnchorY);
                        if (!isWindows11) ImGui::BeginDisabled();
                        bool tempSnap = g_snappyUI.load();
                        ImGui::PushID("toggle_snap");
                        bool snapToggled = PremiumToggle("Reduce Menu & Hover Delays", &tempSnap);
                        ImGui::PopID();
                        ImGui::SetCursorPosY(bottomAnchorY);
                        ImGui::PushID("btn_snap");
                        bool snapButtoned = PremiumButton(tempSnap ? "Restore Default Delays" : "Make UI Snappier", ImVec2(-1, globalBtnHeight));
                        ImGui::PopID();
                        if ((snapToggled || snapButtoned) && isWindows11) {
                            if (snapButtoned) tempSnap = !tempSnap;
                            g_snappyUI = tempSnap; SavePersistentState();
                            OptiMax::Debloat::SetSnappyUI(tempSnap);
                        }
                        if (!isWindows11) ImGui::EndDisabled();
                        ImGui::EndChild();

                        ImGui::EndTable();
                    }

                    // --- Windows Health Repair (DISM + SFC) ---
                    ImGui::Dummy(ImVec2(0, 6 * dpiScale)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 6 * dpiScale));
                    if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Windows Health Repair"); if (m_fontBold) ImGui::PopFont();
                    ImGui::TextWrapped("Runs Microsoft's own repair chain in order: DISM CheckHealth, ScanHealth, RestoreHealth, StartComponentCleanup, then SFC ScanNow. Repairs a damaged component store and replaces corrupted system files - worth doing if Windows Update fails, features break after an upgrade, or things behave oddly for no clear reason. RestoreHealth needs an internet connection.");
                    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "This can take 30+ minutes. It runs in the background - the app stays usable, and every stage is written to the log file.");
                    ImGui::Spacing();

                    {
                        const bool healthBusy = OptiMax::SystemHealth::g_running.load();
                        // Blocked while a game is running. DISM and SFC are not an
                        // anti-cheat concern - they never touch the game process - but
                        // ScanHealth and /scannow both saturate the disk for minutes at
                        // a time, which is exactly the stutter this app exists to remove.
                        // Starting a 30-minute I/O storm underneath someone's game would
                        // undo the entire point of the tool.
                        const bool gameRunning = g_isGameRunning.load();
                        if (gameRunning) {
                            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                                "Unavailable while a game is running - this saturates the disk for minutes at a time. Close the game first.");
                            ImGui::Spacing();
                        }
                        const bool healthBlocked = healthBusy || gameRunning;
                        if (healthBlocked) ImGui::BeginDisabled();
                        if (PremiumButton(healthBusy ? "Health Repair Running..." : "Run Windows Health Repair", ImVec2(-1, globalBtnHeight))) {
                            OptiMax::SystemHealth::g_running = true;
                            // Guarded for the same reason as the UWP threads: an
                            // exception at a thread entry point calls std::terminate,
                            // and this one owns g_running - a throw would leave the
                            // button disabled for the rest of the session.
                            std::thread([]() {
                                try { OptiMax::SystemHealth::RunHealthRepair(); }
                                catch (...) {
                                    SafeAddLog("HEALTH ERROR: The repair sequence failed unexpectedly.");
                                    OptiMax::SystemHealth::SetStatus("Failed", "unexpected error");
                                }
                                OptiMax::SystemHealth::g_running = false;
                            }).detach();
                        }
                        if (healthBlocked) ImGui::EndDisabled();

                        std::string hStage = OptiMax::SystemHealth::GetStatus();
                        if (!hStage.empty()) {
                            std::string hLine = OptiMax::SystemHealth::GetLastLine();
                            ImGui::Spacing();
                            ImGui::TextColored(sexyPurple, "Stage: %s", hStage.c_str());
                            if (!hLine.empty()) { ImGui::TextWrapped("%s", hLine.c_str()); }
                        }
                    }

                    // --- Remove Store Apps (native PackageManager, full-width pick-list) ---
                    static std::vector<OptiMax::UWP::AppEntry> uwpApps;
                    static std::atomic<bool> uwpScanning{ false };
                    static std::atomic<bool> uwpRemoving{ false };
                    static std::atomic<bool> uwpScanned{ false };
                    static std::mutex uwpMutex;

                    ImGui::Dummy(ImVec2(0, 6 * dpiScale)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 6 * dpiScale));
                    if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Remove Store Apps"); if (m_fontBold) ImGui::PopFont();
                    ImGui::TextWrapped("Uninstalls bundled Store apps via the native PackageManager API - no PowerShell. Scan to see which are installed, tick the ones to remove, then remove. Removal runs in the background.");
                    ImGui::Spacing();

                    bool busy = uwpScanning.load() || uwpRemoving.load();
                    if (busy) ImGui::BeginDisabled();
                    if (PremiumButton(uwpScanned.load() ? "Re-scan Installed Apps" : "Scan for Installed Apps", ImVec2(-1, globalBtnHeight))) {
                        uwpScanning = true;
                        // Guarded: an exception at a thread's entry point terminates the
                        // process, and this thread owns uwpScanning - a throw would also
                        // have left the whole Store-apps section disabled for good.
                        std::thread([]() {
                            try {
                                auto found = OptiMax::UWP::ScanInstalled();
                                { std::lock_guard<std::mutex> lk(uwpMutex); uwpApps = std::move(found); }
                                uwpScanned = true;
                            }
                            catch (...) {
                                SafeAddLog("DEBLOAT ERROR: The Store app scan failed unexpectedly.");
                            }
                            uwpScanning = false;
                        }).detach();
                    }
                    if (busy) ImGui::EndDisabled();

                    if (uwpScanning.load()) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Scanning installed apps...");
                    else if (uwpRemoving.load()) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Removing selected apps... (see Application Logs)");

                    {
                        std::lock_guard<std::mutex> lk(uwpMutex);
                        if (uwpScanned.load() && uwpApps.empty() && !uwpScanning.load()) {
                            ImGui::TextWrapped("No removable bloat apps from the catalog were found installed.");
                        }
                        else if (!uwpApps.empty()) {
                            ImGui::BeginChild("UwpList", ImVec2(0, 220 * dpiScale), true);
                            for (size_t i = 0; i < uwpApps.size(); ++i) {
                                ImGui::PushID((int)i);
                                ImGui::Checkbox(uwpApps[i].display.c_str(), &uwpApps[i].selected);
                                ImGui::PopID();
                            }
                            ImGui::EndChild();

                            if (busy) ImGui::BeginDisabled();
                            if (PremiumButton("Remove Selected Apps", ImVec2(-1, globalBtnHeight))) {
                                std::vector<OptiMax::UWP::AppEntry> sel;
                                for (auto& a : uwpApps) if (a.selected) sel.push_back(a);
                                if (!sel.empty()) {
                                    uwpRemoving = true;
                                    std::thread([sel]() {
                                        try {
                                            OptiMax::UWP::RemovePackages(sel);
                                            auto found = OptiMax::UWP::ScanInstalled();  // refresh after removal
                                            { std::lock_guard<std::mutex> lk2(uwpMutex); uwpApps = std::move(found); }
                                        }
                                        catch (...) {
                                            SafeAddLog("DEBLOAT ERROR: Removing Store apps failed unexpectedly. Re-scan to see the current state.");
                                        }
                                        uwpRemoving = false;
                                    }).detach();
                                }
                            }
                            if (busy) ImGui::EndDisabled();
                        }
                    }
                }

                else if (activeTab == 11) {
                    static OptiMax::Discord::DebloatOptions dopt;   // defaults come from the struct
                    static bool discordRemoveLocalStorage = false;
                    static std::string discordStatus;
                    ImVec4 warnColor(1.0f, 0.7f, 0.2f, 1.0f);
                    ImVec4 okColor(0.4f, 0.85f, 0.5f, 1.0f);

                    ImGui::TextWrapped("Slims down your Discord install using native file and registry operations only - no scripts are run. Discord must be fully closed (including the tray icon) before debloating or writing settings.");
                    ImGui::Spacing();

                    // --- Warning / notes box ---
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.10f, 0.02f, 1.0f));
                    ImGui::BeginChild("DiscordNotes", ImVec2(0, 118 * dpiScale), true);
                    if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(warnColor, "Before you run this"); if (m_fontBold) ImGui::PopFont();
                    ImGui::TextWrapped("- Backup (Advanced): saves a full copy of your Discord folder to the Desktop, so you can keep both a debloated and an original Discord.");
                    ImGui::TextWrapped("- After removing the updater, a 'Discord Debloated' shortcut is created on your Desktop that launches Discord directly and skips update checks.");
                    ImGui::TextWrapped("- Cleaning the cache may reset some UI preferences (font size, theme). Your login is kept unless you also remove Local Storage.");
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::Spacing();

                    if (!discordStatus.empty()) {
                        ImGui::TextColored(discordStatus.rfind("OK", 0) == 0 ? okColor : warnColor, "%s", discordStatus.c_str());
                        ImGui::Spacing();
                    }

                    // ================= SECTION 1: DEBLOAT INSTALL =================
                    if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "1. Debloat Install"); if (m_fontBold) ImGui::PopFont();
                    ImGui::TextWrapped("Removes old version folders, unused language packs, junk renderers, and (optionally) modules you don't need. Toggle exactly what you want below.");
                    ImGui::Spacing();

                    if (ImGui::CollapsingHeader("Advanced Options", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::PushID("dopt");
                        PremiumToggle("Create backup to Desktop first", &dopt.backup);
                        PremiumToggle("Remove Krisp (only if you use other noise cancelling)", &dopt.removeKrisp);
                        PremiumToggle("Remove Game SDK DLLs (safe for most users)", &dopt.removeGameSDK);
                        PremiumToggle("Remove Game Presence / RPC (some games need it, e.g. FiveM)", &dopt.removeRPC);
                        PremiumToggle("Remove auto-updater (adds a direct-launch shortcut)", &dopt.removeUpdater);
                        PremiumToggle("Disable autostart (registry Run key)", &dopt.disableAutostart);
                        PremiumToggle("Disable Fullscreen Optimizations for Discord", &dopt.disableFSO);
                        PremiumToggle("Remove junk files (SwiftShader, Vulkan, d3dcompiler, .sig)", &dopt.removeJunk);
                        ImGui::Spacing();
                        ImGui::TextColored(sexyPurple, "Language packs to keep:");
                        PremiumToggle("Keep English", &dopt.keepEnglish);
                        PremiumToggle("Keep French", &dopt.keepFrench);
                        ImGui::PopID();
                    }
                    ImGui::Spacing();
                    if (PremiumButton("Run Discord Debloat", ImVec2(-1, globalBtnHeight))) {
                        if (OptiMax::Discord::IsRunning()) discordStatus = "Discord is still running - close it fully (including the tray icon) and try again.";
                        else if (OptiMax::Discord::RunDebloat(dopt)) discordStatus = "OK - Debloat finished. See Application Logs for details.";
                        else discordStatus = "Nothing was done - no Discord install was found, it is still running, or the backup you asked for could not be made. See Application Logs.";
                    }

                    ImGui::Dummy(ImVec2(0, 8 * dpiScale)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 8 * dpiScale));

                    // ================= SECTION 2: OPTIMIZE SETTINGS =================
                    if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "2. Optimize Settings"); if (m_fontBold) ImGui::PopFont();
                    ImGui::TextWrapped("Writes a performance-focused settings.json (preserving your other settings): hardware acceleration OFF, update checks skipped, developer mode ON, minimize to tray ON, verbose logging OFF, in-app DevTools/debugging OFF, start maximized.");
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Note: animate-stickers, desktop notifications and Clips are account settings synced from Discord's servers - they can't be forced by editing local files, only in Discord's own settings.");
                    ImGui::Spacing();

                    if (ImGui::CollapsingHeader("How to set the account-only options in Discord")) {
                        ImGui::Indent();
                        ImGui::TextWrapped("These live on your Discord account, so set them once in the Discord app (User Settings, the gear icon near your name) and they follow your account everywhere:");
                        ImGui::Spacing();
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Animate stickers on interaction"); if (m_fontBold) ImGui::PopFont();
                        ImGui::BulletText("Settings > Accessibility > Stickers > Animate Stickers > choose \"On Interaction\".");
                        ImGui::Spacing();
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Disable desktop notifications"); if (m_fontBold) ImGui::PopFont();
                        ImGui::BulletText("Settings > Notifications > turn off \"Enable Desktop Notifications\".");
                        ImGui::BulletText("Optional: also turn off \"Enable Unread Message Badge\" and \"Taskbar Flashing\".");
                        ImGui::Spacing();
                        if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Disable Clips"); if (m_fontBold) ImGui::PopFont();
                        ImGui::BulletText("Settings > Voice & Video > scroll to Clips > turn off \"Enable Clips\".");
                        ImGui::Spacing();
                        ImGui::TextDisabled("(OptiMax can't change these for you - the only programmatic route is reading your Discord token, which is exactly what account-stealer malware does and violates Discord's Terms.)");
                        ImGui::Unindent();
                        ImGui::Spacing();
                    }

                    if (PremiumButton("Apply Optimized Settings", ImVec2(-1, globalBtnHeight))) {
                        if (OptiMax::Discord::IsRunning()) discordStatus = "Close Discord first - it overwrites settings.json on exit.";
                        else if (OptiMax::Discord::WriteOptimizedSettings()) discordStatus = "OK - Optimized settings.json written.";
                        else discordStatus = "Could not write settings.json - is Discord installed and closed?";
                    }

                    ImGui::Dummy(ImVec2(0, 8 * dpiScale)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 8 * dpiScale));

                    // ================= SECTION 3: CLEAN CACHE =================
                    if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "3. Clean Cache"); if (m_fontBold) ImGui::PopFont();
                    ImGui::TextWrapped("Clears accumulated runtime data (Cache, GPUCache, ShaderCache, Crashpad, logs, and more). Your account session is preserved unless you remove Local Storage.");
                    ImGui::Spacing();
                    ImGui::PushID("dcache");
                    PremiumToggle("Also remove Local Storage (frees more space, signs you out)", &discordRemoveLocalStorage);
                    ImGui::PopID();
                    ImGui::Spacing();
                    if (PremiumButton("Clean Discord Cache", ImVec2(-1, globalBtnHeight))) {
                        if (OptiMax::Discord::IsRunning()) discordStatus = "Close Discord first so its cache files aren't locked.";
                        else { OptiMax::Discord::CleanCache(discordRemoveLocalStorage); discordStatus = "OK - Cache cleaned. See Application Logs for details."; }
                    }
                }

                ImGui::EndChild();
                ImGui::End();
            }

            ImGui::Render();

            if (g_mainRenderTargetView && g_pd3dDeviceContext) {
                const float clearColor[4] = { 0.002f, 0.002f, 0.002f, 1.0f };
                g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
                g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }

            if (g_pSwapChain) { g_pSwapChain->Present(1, 0); }

            // THE FIX (2 of 2): this was an unconditional Sleep(16) on top of a
            // vsync Present, so the whole UI was rebuilt and redrawn ~30 times a
            // second for as long as the window existed - focused or not, in view
            // or buried behind a game. Measured at idle on a 5800X3D: a steady
            // ~16% of one core, doing nothing, on a tool whose entire purpose is
            // to give that back.
            //
            // A window nobody is looking at does not need 30 fps. Foreground
            // keeps the old cadence exactly, so interaction is unchanged; a
            // background window drops to ~8 fps, which is still live enough to
            // watch a graph move while you are in another app.
            //
            // MsgWaitForMultipleObjects rather than Sleep so input is never
            // waiting on the throttle: any message wakes it immediately, and
            // clicking the window makes it foreground on that same wake.
            const bool foreground = (::GetForegroundWindow() == hwnd);
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, foreground ? 16 : 100, QS_ALLINPUT);
        }
        return 0;
    }
};