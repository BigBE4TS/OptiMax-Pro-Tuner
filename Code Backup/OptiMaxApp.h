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
#include "OptiMaxVersion.h"   // the single version source - see CURRENT_VERSION below
#include "OptiMaxGlobals.h"
// Explicit rather than inherited through OptiMaxEngine.h. This file converts
// game names at half a dozen boundaries and the conversion must not depend on
// which header happened to pull it in first - that is the include-order
// fragility CLAUDE.md records as having broken this build twice.
#include "OptiMaxText.h"
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
#include "OptiMaxDiagnostics.h"
// After Diagnostics, because the governor acts on what the monitor measures.
// Both are self-contained (Attribution pulls in winevt.h itself), so neither
// depends on anything declared in this file - which is what keeps them out of
// the include-order trap that has broken this build twice.
#include "OptiMaxSelfGovernor.h"
// Maps a detected stall cause to a suggested fix. Pure mapping - it decides
// WHAT should happen and deliberately cannot see the tweak flags, which are
// declared further down this file. The app layer resolves the suggestion
// against the live state and carries it out.
#include "OptiMaxRemedy.h"
#include "OptiMaxChangelog.h"

// UI & Tweaks Includes
#include "OptiMaxUi_Widgets.h"
#include "OptiMaxUi_Cards.h"   // v4.3.0 dashboard composites - KPI tiles, dials, toggle rows, the stall timeline
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

// v4.3.0: the version whose "What's New" has already been shown, and the flag
// that asks the UI to show it. Kept separate so a deferred prompt (a game is
// running) is not mistaken for one that has been seen.
extern std::string g_lastSeenVersion;
extern std::atomic<bool> g_showChangelogPrompt;
extern std::atomic<bool> g_settingSafeMode;
extern std::atomic<bool> g_settingMpoDisable;
extern std::atomic<bool> g_settingAutoRam;
extern std::atomic<int> g_ramCleanThreshold;
// Opt-in, DEFAULT OFF, and it must stay that way. When on, a stall whose cause
// maps to a tweak that is reversible and takes effect immediately gets that
// tweak applied without asking. Nothing reboot-gated is ever eligible - it
// could not fix the stall that suggested it, and a change the user never
// watched happen is the worst kind to make silently.
extern std::atomic<bool> g_settingAutoRemedy;

extern std::mutex g_configMutex;

// THE FEATURE: resolves whether P-cores should be locked for a specific game,
// checking its per-game override first and falling back to the global default.
// exeName is expected as a wide string (matches activeGameName's type at the
// call site); converted narrow for the override map lookup.
// THE FIX: this used to sit above g_configMutex's own extern declaration and
// failed to compile as a result - moved below everything it depends on.
inline bool ShouldLockPCoresForGame(const std::wstring& exeName) {
    // UTF-8, not a narrowing cast - the override map is keyed by the same
    // strings the library holds, so both sides must agree on the encoding or
    // a non-ASCII game silently misses its own per-game override.
    std::string narrowName = OptiMax::Text::ToUtf8(exeName);
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
    std::string narrowName = OptiMax::Text::ToUtf8(exeName);   // see ShouldLockPCoresForGame
    std::lock_guard<std::mutex> lock(g_configMutex);
    auto it = flagMap.find(narrowName);
    return (it != flagMap.end()) ? it->second : false;
}

extern std::vector<std::string> g_targetGames;

// Resolves the game currently being played, or "" if none. Exists so callers
// that need to AVOID touching the game can find it without duplicating the
// telemetry loop's detection - see EnableExtremeMode, which throttled whatever
// the user was playing until it was given this.
//
// Deliberately does NOT live in OptiMaxEngine.h with the throttling code, even
// though that is where it would be used: the library lives in g_targetGames,
// which is declared here, and reaching down from the engine into app-level state
// is the include-order dependency that has already broken this build twice.
// The name is passed down as a parameter instead, matching SmartBackgroundPurge.
//
// No debounce, unlike the telemetry loop's fullscreen fallback. For protection
// the asymmetry is deliberate: a false positive costs one process not being
// throttled, a false negative costs the user frames in the game they are playing.
inline std::wstring GetActiveGameExe() {
    std::vector<std::string> localGames;
    { std::lock_guard<std::mutex> lock(g_configMutex); localGames = g_targetGames; }

    std::vector<std::wstring> wTargets;
    wTargets.reserve(localGames.size());
    for (const auto& g : localGames) wTargets.push_back(OptiMax::Text::FromUtf8(g));

    std::wstring match = OptiMax::AutoBoost::FindRunningTarget(wTargets);
    if (!match.empty() && !OptiMax::IsProtectedProcess(match) && !OptiMax::IsKnownNonGameApp(match)) {
        return match;
    }

    // Same zero-configuration fallback the telemetry loop uses: a fullscreen
    // foreground process that is not a known shell/system app is a game.
    // GetFullscreenForegroundGame already applies both exclusion lists.
    return OptiMax::GetFullscreenForegroundGame();
}

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
// background process, so "Turn Off Main Tweaks" (then named "All") left downloads, backups and file
// transfers still crawling while the log said everything was off.
// Undoes background app throttling. The walk-every-process version that used to
// live here declared every non-whitelisted process NORMAL whether OptiMax had
// touched it or not; the reasoning and the trade-offs are written down at
// OptiMax::RestoreThrottledPriorities, which now does the work. Kept as a wrapper
// because five call sites use this name and the log line belongs at this level.
inline void RevertIsolationPriorities() {
    int restored = OptiMax::RestoreThrottledPriorities();
    if (restored > 0) {
        SafeAddLog("SYSTEM: Restored " + std::to_string(restored) + " background app(s) to the priority they had before throttling.");
    }
    else {
        SafeAddLog("SYSTEM: No background apps needed restoring - none were throttled in this session.");
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
    // Third size, for dashboard numbers and dial readouts. See the font block in
    // Init() for why the glyph ranges there are explicit.
    ImFont* m_fontNumber = nullptr;

    bool done = false;

    // ======================================================================
    // NAVIGATION - six destinations, named.
    //
    // These used to be bare integers 0-11, numbered in the order the features
    // were added, so the sidebar read Dashboard 0, Power 1, Services 2,
    // Latency 3 ... and Debloat 10 sat under SYSTEM TWEAKS between Services
    // and Discord. Nothing in the numbering said where a page belonged, and
    // every page block was guarded by a magic number.
    //
    // The four tweak pages are now one (TAB_TWEAKS), and the two destinations
    // that legitimately carry more than one view get a sub-tab rather than
    // more sidebar rows - the sidebar is the thing this merge set out to
    // shorten, so paying for it in rail entries would defeat the point.
    // ======================================================================
    enum : int {
        TAB_DASHBOARD = 0,
        TAB_TWEAKS,
        TAB_GAMES,
        TAB_DIAGNOSTICS,
        TAB_DISCORD,
        TAB_SETTINGS,
    };
    int activeTab = TAB_DASHBOARD;
    int gamesSubTab = 0;    // 0 My Games, 1 Community Profiles
    // Starts on Live Monitoring, which is the FIRST tab - the values are the
    // original page numbers and deliberately do not match the tab order, so
    // that reordering the bar again cannot silently renumber a page block.
    int diagSubTab = 1;    // 0 Latency Test, 1 Live Monitoring, 2 Logs
    // Set by the Dashboard's "Edit game library" jump and consumed by the
    // Games tab bar. Without it the jump lands on whichever sub-tab was last
    // open, which for a button labelled "Edit game library" is wrong.
    bool gamesForceLibrary = false;

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
    // ✅ THE TEN SITES ARE NOW ONE. Bumping the version is a single edit to
    // OptiMaxVersion.h, and everything downstream - this constant, the five
    // resource.rc fields, the four literals in OptiMax_Profiler.cpp, the git
    // tag and version.txt that release.ps1 derives - follows from it.
    //
    // Kept as a named constant rather than using the macro at every use site,
    // because release.ps1 and the updater both talk about "CURRENT_VERSION"
    // and it is the name this codebase reasons in.
    const std::string CURRENT_VERSION = OPTIMAX_VERSION_STRING;

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
    // ------------------------------------------------------------------
    // Core-tweak apply/revert, extracted so the Dashboard's control rows and
    // the "Core System Tweaks" card share ONE copy.
    //
    // Pasting the sequence into the new rows would have been faster and would
    // have created exactly the defect this project keeps finding: two code
    // paths that are supposed to do the same thing, drifting apart the first
    // time one of them is edited. The revert half is the half that matters -
    // it has a conditional (RestoreProcessorPowerSettings is skipped when the
    // standalone "Keep All Cores Awake" toggle owns those settings) and a
    // second copy would eventually lose it.
    // ------------------------------------------------------------------
    void ApplyCoreTweaksNow(bool antiCheatActive) {
        // Wraps the restore point's own scope rather than duplicating it -
        // AttributeWindow keeps only the longest overlapping record, so the
        // nesting collapses to one honest answer instead of two. Worth having
        // even though the restore point is the expensive part: with Safe Mode
        // off there is no restore point at all, and EngageUltimatePower can
        // still take a moment.
        OptiMax::Diagnostics::Attribution::SelfActivity::Scope _self("applying core tweaks");

        RestorePointBeforeTweak();
        g_isCoreActive = true; SavePersistentState();
        OptiMax::SetTimer(true, antiCheatActive); OptiMax::SetGPUPriorityHigh();
        OptiMax::Power::EngageUltimatePower(); OptiMax::AdvancedPower::DisableCoreParking();
        OptiMax::AdvancedPower::SetEPPToPerformance(); OptiMax::OptimizeNetworkLatency(true);
        SafeAddLog("SYSTEM: Core system tweaks applied.");
    }

    void RevertCoreTweaksNow() {
        g_isCoreActive = false; SavePersistentState();
        OptiMax::RevertOptimizations();
        // Applying Core Tweaks disables core parking and pins EPP to maximum;
        // RevertOptimizations() undoes neither. Left alone when the standalone
        // "Keep All Cores Awake" toggle owns those settings.
        if (!g_coresUnparked.load()) {
            OptiMax::AdvancedPower::RestoreProcessorPowerSettings();
        }
        SafeAddLog("SYSTEM: Core system tweaks reverted.");
    }

    void RestorePointBeforeTweak() {
        if (g_settingSafeMode.load()) OptiMax::CreateRestorePoint();
    }

    // ======================================================================
    // REMEDY PLUMBING - the app half of OptiMaxRemedy.h.
    //
    // That header decides WHAT should happen from a stall's cause and cannot
    // see the tweak flags, because it sits below this file in the include
    // order. These two resolve its Action against the live state and carry it
    // out, which is the only place both halves are visible.
    // ======================================================================

    // Already in the state the advice is asking for? A suggestion to turn on
    // something that is already on is noise, and an apply button for it would
    // be a no-op the user cannot tell from a failure.
    bool RemedyAlreadySatisfied(OptiMax::Diagnostics::Remedy::Action a) const {
        using A = OptiMax::Diagnostics::Remedy::Action;
        switch (a) {
        case A::CoreTweaks:     return g_isCoreActive.load();
        case A::KeepCoresAwake: return g_coresUnparked.load();
        case A::AutoBoost:      return g_autoBoostEnabled.load();
        case A::DisableMPO:     return g_settingMpoDisable.load();
        case A::None:           break;
        }
        return true;   // nothing to do counts as satisfied
    }

    // Returns whether the machine actually ended up in the requested state, so
    // the caller can report honestly rather than assuming. Every branch here
    // goes through the same function the corresponding toggle uses - there is
    // no second implementation to drift.
    bool ApplyRemedy(OptiMax::Diagnostics::Remedy::Action a, bool antiCheatActive) {
        using A = OptiMax::Diagnostics::Remedy::Action;
        switch (a) {
        case A::CoreTweaks:
            ApplyCoreTweaksNow(antiCheatActive);
            return g_isCoreActive.load();
        case A::KeepCoresAwake: {
            const bool coresOk = OptiMax::AdvancedPower::DisableCoreParking();
            const bool eppOk = OptiMax::AdvancedPower::SetEPPToPerformance();
            if (coresOk && eppOk) { g_coresUnparked = true; SavePersistentState(); return true; }
            return false;
        }
        case A::AutoBoost:
            g_autoBoostEnabled = true; SavePersistentState();
            return true;
        case A::DisableMPO:
            if (OptiMax::Profiler::SetMPODisabled(true)) {
                g_settingMpoDisable = true; SavePersistentState();
                return true;
            }
            return false;
        case A::None:
            break;
        }
        return false;
    }

    // ONE place builds the report context, because there are two Copy Report
    // buttons - the Dashboard's and Live Monitoring's - and two constructions of
    // "the same" report is exactly the drift this project keeps paying for.
    // Supplies what OptiMaxDiagnostics.h cannot see for itself: the version, the
    // OS string, and whether a suggested tweak is already on.
    OptiMax::Diagnostics::ReportContext MakeReportContext() {
        OptiMax::Diagnostics::ReportContext ctx;
        ctx.version = CURRENT_VERSION;
        ctx.os = hwOS;
        ctx.isAlreadyOn = [this](OptiMax::Diagnostics::Remedy::Action a) {
            return RemedyAlreadySatisfied(a);
            };
        return ctx;
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

    // v4.3.0: the sparklines and the 60-second graph need a past, and the
    // telemetry loop was sampling these every tick and keeping nothing. 60
    // entries is a minute at the 1Hz cadence. Written by the telemetry thread,
    // read by the UI thread - see the note in OptiMaxUi_Cards.h on why a fixed
    // buffer rather than a growable container is the safe choice for that.
    OptiMaxUi::MetricHistory<60> histCpu, histRam, histGpu, histDpc;

    // Last stall sequence the automatic-remedy pass has looked at. Touched only
    // from the telemetry worker, so it needs no synchronisation of its own -
    // g_stallSeq is the atomic, and this is just what we last saw it at.
    unsigned long long lastRemedySeq = 0;

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

            // A5-3: closes the blind spot that made the 2026-07-28 freeze
            // report cost an hour of manual event-log correlation. This loop
            // already wakes once a second, so the gap between wakes is a
            // free system-wide responsiveness sample. See OptiMaxDiagnostics.h.
            OptiMax::Diagnostics::StallMonitor stallMonitor;

            // v4.3.0: what OptiMax does with what the monitor measures. Decides
            // only OptiMax's own behaviour - never Windows' - for the reasons
            // written at the top of OptiMaxSelfGovernor.h.
            OptiMax::Diagnostics::SelfGovernor::LoadGate loadGate;
            DWORD nextTickMs = OptiMax::Diagnostics::SelfGovernor::kBaseTickMs;
            bool purgeDeferralLogged = false;

            // v4.3.0: stall detection lives on its own thread now. Owned here, in
            // the telemetry worker's scope, so its lifetime is bounded by the same
            // g_isAppRunning loop and its destructor joins on the way out - no
            // detached thread outliving the log file it writes to.
            OptiMax::Diagnostics::Watchdog watchdog;
            watchdog.Start();

            while (g_isAppRunning) {
                stallMonitor.BeginTick();

                bool currentAntiCheat = OptiMax::IsAntiCheatRunning();
                g_isAntiCheatActive = currentAntiCheat;

                // Per-process CPU sample for stall attribution. Sits here, on
                // the 1Hz telemetry tick, rather than on the watchdog thread -
                // that thread must do nothing but sleep and check a clock, for
                // the same reason the automatic remedy is not driven from it.
                //
                // 🎯 The placement is also what makes the measurement work.
                // This loop is the one that goes quiet when the system stalls
                // (a tick came back 11.7 seconds late during testing), so
                // consecutive samples naturally BRACKET a freeze instead of
                // landing inside it - which is exactly the window the delta
                // needs to cover.
                //
                // MEASURED, not assumed: 6-8ms per sample across 88 distinct
                // executables on the dev machine (optimax_test_procload test
                // 11, which fails the build if it ever crosses the 250ms stall
                // threshold - a sampler that manufactured the stall it explains
                // would be worse than none). That is one EnumProcesses plus an
                // OpenProcess per pid, which is roughly a second copy of what
                // IsAntiCheatRunning immediately above already pays every tick.
                OptiMax::Diagnostics::Attribution::ProcessLoad::PushSample();

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

                // THE FIX: this was gated on isProfilerRunning alone - the "Show
                // Live Readings" switch on the Live Monitoring page, which is off by
                // default. The v4.3.0 Dashboard shows CPU, memory, DPC and timer
                // as well, so on a fresh launch every tile read 0 and every
                // sparkline was flat. Sampling now also runs while the Dashboard
                // is the visible page.
                const bool wantMetrics = isProfilerRunning || (activeTab == TAB_DASHBOARD);
                if (wantMetrics) {
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

                    // 🔴 NOT sampled while a game is running, and this gate is the
                    // reason widening the block above was safe. Every call to
                    // GetMaxDPCSpikeMicroseconds spins 10,000 QueryPerformanceCounter
                    // iterations at THREAD_PRIORITY_TIME_CRITICAL. Doing that once
                    // a second, forever, because someone left the Dashboard open
                    // during a match is precisely the defect the 60-second latency
                    // test was gated for in v4.2.2 - the tool measurably hurting
                    // the thing it was turned on to protect.
                    //
                    // The tile keeps showing the last reading rather than going
                    // blank: a stale latency figure is honest if the label says so,
                    // and pegging a core to refresh it is not worth a live number.
                    if (!g_isGameRunning.load()) {
                        cachedDpcSpike = OptiMax::Profiler::GetMaxDPCSpikeMicroseconds();
                    }

                    // Keep the past. Pushed here rather than from the UI thread
                    // because the UI runs at display rate - sampling there would
                    // make "the last 60 points" mean a different length of time
                    // on every machine, and on a 240Hz panel it would be four
                    // seconds of history rather than a minute.
                    histCpu.Push((float)cachedCpuLoad);
                    histGpu.Push((float)cachedGpuLoad);
                    histRam.Push((float)cachedRam);
                    histDpc.Push((float)cachedDpcSpike);
                }

                // ==========================================================
                // AUTOMATIC REMEDIES - opt-in, default off.
                //
                // 🔴 RUNS HERE AND NOT ON THE WATCHDOG THREAD, deliberately.
                // That thread must do nothing but sleep and check a clock;
                // work done on it is time not spent measuring, which is the
                // detection gap the attribution cooldown was added to reduce.
                // Applying a tweak there would reintroduce it, larger.
                //
                // Driven off g_stallSeq rather than a queue: the watchdog
                // already bumps it per stall, so a changed value means "there
                // is something new to look at" without any new shared state.
                // ==========================================================
                if (g_settingAutoRemedy.load()) {
                    const unsigned long long seq = OptiMax::Diagnostics::g_stallSeq.load();
                    if (seq != lastRemedySeq) {
                        lastRemedySeq = seq;

                        const bool acActive = g_isAntiCheatActive.load();
                        for (const auto& r : OptiMax::Diagnostics::SnapshotStalls()) {
                            if (r.rateLimited) continue;   // no lookup ran - nothing to act on

                            namespace Rem = OptiMax::Diagnostics::Remedy;
                            const Rem::Advice adv = r.attributed
                                ? Rem::ForCause(r.kind)
                                : Rem::ForUnattributed(r.allProbesRan);

                            // Four gates, and every one of them is load-bearing:
                            //   autoSafe   - reversible, immediate, no security
                            //                effect. Excludes everything
                            //                reboot-gated by construction.
                            //   satisfied  - do not re-apply what is already on,
                            //                or every stall re-logs the same line.
                            //   anti-cheat - same rule as everywhere else. None
                            //                of these open a handle on the game,
                            //                but a tuning tool changing system
                            //                state unprompted mid-match is the
                            //                behaviour the gate exists for.
                            if (!adv.autoSafe) continue;
                            if (RemedyAlreadySatisfied(adv.action)) continue;
                            if (acActive) continue;

                            const bool ok = ApplyRemedy(adv.action, acActive);
                            // Logged either way and named as automatic, so a
                            // change the user did not click is still something
                            // they can find afterwards.
                            SafeAddLog(std::string("REMEDY: Automatically ")
                                + (ok ? "applied " : "FAILED to apply ")
                                + Rem::ActionLogName(adv.action)
                                + " after a stall (" + r.cause + ").");
                            break;   // one per pass; the next stall re-evaluates
                        }
                    }
                }

                bool curR = false;
                std::wstring activeGameName = L"";
                {
                    std::vector<std::string> localGames;
                    { std::lock_guard<std::mutex> lock(g_configMutex); localGames = g_targetGames; }

                    std::vector<std::wstring> wTargets;
                    wTargets.reserve(localGames.size());
                    for (const auto& game : localGames) {
                        wTargets.push_back(OptiMax::Text::FromUtf8(game));
                    }

                    std::wstring match = OptiMax::AutoBoost::FindRunningTarget(wTargets);

                    // Self-heal: an earlier build could auto-add an overlay/shell process
                    // (e.g. screenclippinghost.exe from a Win+Shift+S snip) to the library
                    // before those were excluded. Such an entry would match here and engage
                    // Auto-Boost immediately, re-creating the lag. If a match is actually a
                    // protected or known-non-game process, drop it from the library and
                    // ignore it.
                    if (!match.empty() && (OptiMax::IsProtectedProcess(match) || OptiMax::IsKnownNonGameApp(match))) {
                        // Must match the encoding the library was written in, or
                        // the erase below silently removes nothing and this
                        // self-heal logs a removal that did not happen.
                        std::string bad = OptiMax::Text::ToUtf8(match);
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
                            // The auto-grow path writes straight into the
                            // persisted library, so it has to produce the same
                            // UTF-8 the browse path does - otherwise a
                            // non-ASCII game would be re-added on every launch,
                            // never matching the entry it wrote last time.
                            std::string narrowName = OptiMax::Text::ToUtf8(fsGame);
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
                        // THE FIX: this was TrueStandbyPurge(), which does two things that
                        // are wrong at this exact moment. It has no game exclusion, so it
                        // trims the working set of the game that just launched; and it
                        // purges the system-wide standby list, dropping the OS file cache
                        // the game is about to stream its assets through. That is the same
                        // call, for the same reason, that the periodic cleaner below
                        // already stopped making - and SmartBackgroundPurge's own header
                        // says to use it instead "whenever a game is currently running",
                        // which is exactly the case on this line.
                        //
                        // It was also the expensive half of a 3030ms tick measured by the
                        // stall detector at game detect (v4.2.2 GUI pass, 22:10:58) - three
                        // seconds of eviction competing with the game's own load I/O.
                        int trimmedForGame = OptiMax::SmartBackgroundPurge(activeGameName);
                        SafeAddLog("RAM: Trimmed " + std::to_string(trimmedForGame) +
                            " background app(s) for the game session (the game's own memory and the file cache are left alone).");
                        g_isCoreActive = true;
                        OptiMax::SetTimer(true, currentAntiCheat);
                        OptiMax::SetGPUPriorityHigh();
                        OptiMax::Power::EngageUltimatePower();
                        OptiMax::AdvancedPower::DisableCoreParking();
                        OptiMax::AdvancedPower::SetEPPToPerformance();
                        OptiMax::OptimizeNetworkLatency(true);

                        // Tell Windows the machine is in use, so idle-triggered
                        // background work does not start mid-match. Called from
                        // here rather than from RevertOptimizations/engage inside
                        // OptiMaxEngine.h because OptiMaxPower.h is included AFTER
                        // OptiMaxEngine.h - the engine cannot see GameSession, and
                        // reaching across that boundary is the include-order
                        // dependency that has broken this build twice.
                        OptiMax::GameSession::Begin(activeGameName);

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
                        OptiMax::GameSession::End();
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
                    //
                    // THE FIX: this measured every result against 0.5ms and warned
                    // "Requested 0.5ms ... Latency gains may not be applied" whenever it
                    // missed. But safe mode never requests 0.5ms - with an anti-cheat
                    // running, SetTimer deliberately takes timeBeginPeriod(1), and 1ms is
                    // then the correct outcome, not a shortfall. So every anti-cheat user
                    // was warned of a failure that had not happened, on every game launch,
                    // naming a target the app had chosen not to ask for. Verified against
                    // Vanguard in the v4.2.2 GUI pass: 0.999600ms reported as a WARNING.
                    // The expected value now follows whichever path SetTimer actually took.
                    if (!timerResolutionVerifiedThisSession) {
                        double actualRes = OptiMax::Profiler::GetCurrentTimerResolution();
                        const double requestedMs = currentAntiCheat ? 1.0 : 0.5;
                        const char* requestedTxt = currentAntiCheat ? "1.0" : "0.5";
                        // Tolerance absorbs the OS reporting 0.4992 / 0.9996 rather than
                        // the exact figure, without being loose enough to let a genuine
                        // fallback from 0.5ms to 1ms pass as success.
                        if (actualRes > requestedMs + 0.1) {
                            SafeAddLog(std::string("WARNING: Requested ") + requestedTxt +
                                "ms timer resolution but OS reports " + std::to_string(actualRes) +
                                "ms. Latency gains may not be applied.");
                        }
                        else if (currentAntiCheat) {
                            SafeAddLog("TIMER: Verified " + std::to_string(actualRes) +
                                "ms resolution active (1ms is the anti-cheat-safe path, not a downgrade).");
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

                        // Cheap tier only in-game (no event log probe): the
                        // window here is 15-30s and the smart purge is already
                        // the safe one, so paying 35ms for a probe on the game
                        // path is not worth it. The stall history is free and
                        // catches the case that matters - piling a trim on top
                        // of a machine that just stalled.
                        auto gate = loadGate.ShouldStartHeavyWork(false);
                        if (overThreshold && (now - lastSmartPurgeTime > smartCooldown) && gate.Proceeds()) {
                            int n = OptiMax::SmartBackgroundPurge(activeGameName);
                            SafeAddLog("RAM: Trimmed " + std::to_string(n) + " background app(s) (the running game was left untouched).");
                            lastSmartPurgeTime = now;
                            purgeDeferralLogged = false;
                        }
                        else if (overThreshold && (now - lastSmartPurgeTime > smartCooldown) && !purgeDeferralLogged) {
                            // Said once per episode, not once per second. The
                            // cooldown timestamp is deliberately NOT advanced, so
                            // this retries as soon as the machine settles rather
                            // than silently skipping a cycle.
                            SafeAddLog("RAM: Cleaning deferred - " + gate.reason + ".");
                            purgeDeferralLogged = true;
                        }
                    }
                    else {
                        // Idle path gets the full check including the event log
                        // probe. This fires at most once a minute, so 35ms is
                        // nothing - and this is the purge most likely to collide
                        // with a scheduled Defender scan, since both wait for the
                        // machine to be idle.
                        auto gate = (overThreshold && (now - lastFullPurgeTime > 60000))
                            ? loadGate.ShouldStartHeavyWork(true)
                            : OptiMax::Diagnostics::SelfGovernor::GateDecision{};

                        if (overThreshold && (now - lastFullPurgeTime > 60000) && !gate.Proceeds()
                            && !purgeDeferralLogged) {
                            SafeAddLog("RAM: Cleaning deferred - " + gate.reason + ".");
                            purgeDeferralLogged = true;
                        }

                        if (overThreshold && (now - lastFullPurgeTime > 60000) && gate.Proceeds()) {
                            purgeDeferralLogged = false;
                            // Was TrueStandbyPurge(), which also purged the standby list
                            // (the OS file cache) every 60s. Dropping the whole file cache
                            // on a timer is what made the first Win+Shift+S / Start / Search
                            // after a purge cold-read from disk and stall. SmartBackgroundPurge
                            // trims background working sets (the actual RAM win) but leaves the
                            // standby list warm. The manual "Clean RAM Now" button still does
                            // the full standby purge for anyone who explicitly wants it.
                            int n = OptiMax::SmartBackgroundPurge(L"");
                            SafeAddLog("RAM: Trimmed " + std::to_string(n) + " background app(s) (file cache left warm for responsiveness).");
                            lastFullPurgeTime = now;
                        }
                    }
                }

                // Replaces a bare Sleep(1000). The monitor owns the wait so it
                // can measure how much longer than it asked for the thread
                // actually took to come back - that overshoot is the stall signal.
                //
                // v4.3.0: the result feeds the governor, and the governor decides
                // the next interval. Capped at kMaxTickMs, because this tick is
                // what notices a game starting - backing off without limit would
                // trade a diagnostic nicety for the app's primary job.
                const auto tick = stallMonitor.EndTickAndSleep(nextTickMs, curR);
                loadGate.NoteTick(tick.stalled, tick.ownWorkSlow);
                nextTickMs = OptiMax::Diagnostics::SelfGovernor::AdaptiveTickMs(
                    OptiMax::Diagnostics::SelfGovernor::kBaseTickMs,
                    loadGate.ConsecutiveSlowTicks());
            }

            // Stopped BEFORE the summary, so the counts it reports cannot change
            // between being read and being printed - and so the watchdog is not
            // still writing log lines after the summary claims to be final.
            watchdog.Stop();

            // Logged on the way out, while the log file is still open. Runs on
            // the clean-exit path only; if the guard below catches, the error
            // it reports is the more useful line anyway.
            stallMonitor.LogSessionSummary();

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

        // ------------------------------------------------------------------
        // FONTS (v4.3.0 UI pass)
        //
        // ImGui only rasterises the glyph ranges you ask for, and the default is
        // Latin. The symbols used as icons in this UI - the lightning bolt, the
        // gear, the cloud - live well outside that, so without an explicit range
        // every one of them renders as a '?'. They are all present in Segoe UI
        // itself, so this needs NO extra font file: listing the codepoints is
        // enough. Kept as an explicit list rather than a broad range because a
        // wide range bloats the atlas with thousands of glyphs nothing draws.
        //
        // The ranges array must stay alive until Fonts->Build(), which the
        // backend calls later - hence static rather than a local.
        // 🔴 THE ICONS ARE NOT IN SEGOE UI. Measured, not assumed: of the fifteen
        // codepoints this UI uses as icons, segoeui.ttf contains exactly TWO (the
        // micro sign and the up triangle). seguisym.ttf - Segoe UI Symbol - has
        // all fifteen. segmdl2.ttf has none of them, because MDL2 puts its icons
        // in the private-use area at 0xE700+ under its own numbering.
        //
        // The first build of this UI listed these ranges on the BASE font only,
        // so every icon fell back to the same missing-glyph box and all five KPI
        // tiles appeared to carry an identical diamond. The fix is a merge pass:
        // add the base face, then add the symbol face with MergeMode so its
        // glyphs land in the same atlas and inline with text.
        static const ImWchar latinRange[] = { 0x0020, 0x00FF, 0 };
        static const ImWchar symbolRanges[] = {
            0x2190, 0x21FF,   // arrows
            0x2300, 0x23FF,   // technical: the stopwatch
            0x2500, 0x27BF,   // shapes, misc symbols, dingbats
            0x2B00, 0x2BFF,   // supplemental shapes
            0,
        };

        // MergeMode merges into the font added immediately before it, so each
        // size needs its own pass. PixelSnapH keeps the glyphs from landing on a
        // half pixel and looking soft next to crisp text.
        auto addFace = [&](const char* path, float px) -> ImFont* {
            ImFont* f = io.Fonts->AddFontFromFileTTF(path, px, nullptr, latinRange);
            if (f) {
                ImFontConfig merge;
                merge.MergeMode = true;
                merge.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", px, &merge, symbolRanges);
            }
            return f;
            };

        // ⚠️ THE DENSITY DECISION, SETTLED 2026-07-31 - and it went UP, not down.
        //
        // The open question was whether to drop the 20px base toward the
        // mockup's 13.5px, which is where the app's looser feel came from. The
        // owner's call on seeing it built was the opposite: bigger, and the
        // sidebar especially. So 20 -> 23 base, 24 -> 28 headings, 34 -> 42
        // numerals. Do not "restore" these to the mockup's figures - the mockup
        // was a browser at CSS pixel sizes and this is a 1440p desktop app.
        //
        // This is safe to change precisely because the v4.3.0 layout work
        // derives every offset from font metrics rather than from constants -
        // that was the point of doing it, and this is the change that tests it.
        m_fontRegular = addFace("C:\\Windows\\Fonts\\segoeui.ttf", 23.0f * dpiScale);
        m_fontBold = addFace("C:\\Windows\\Fonts\\segoeuib.ttf", 28.0f * dpiScale);

        // A third size, for the numbers on the dashboard tiles and the dials.
        // Big numerals are most of why the reference designs read as modern, and
        // the app had only two sizes. Falls back to bold rather than to nothing,
        // so a missing file costs the emphasis and not the reading.
        m_fontNumber = addFace("C:\\Windows\\Fonts\\segoeuib.ttf", 42.0f * dpiScale);

        if (!m_fontRegular) m_fontRegular = io.Fonts->AddFontDefault();
        if (!m_fontBold) m_fontBold = m_fontRegular;
        if (!m_fontNumber) m_fontNumber = m_fontBold;

        // UI STYLING
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(16 * dpiScale, 16 * dpiScale);
        style.FramePadding = ImVec2(12 * dpiScale, 6 * dpiScale);
        style.ItemSpacing = ImVec2(10 * dpiScale, 12 * dpiScale);
        // v4.3.0 UI pass: rounder, on the owner's call after comparing mockups.
        // Cards 10 -> 18. Frames 6 -> 10, which is as close to a pill as a shared
        // FrameRounding gets - a true pill needs radius = height/2 and frame
        // heights vary, so anything drawn by hand (toggles, chips) uses its own
        // half-height radius instead of this value.
        style.ChildRounding = 18.0f;
        style.FrameRounding = 10.0f;
        style.ScrollbarRounding = 10.0f;
        style.GrabRounding = 10.0f;
        style.TabRounding = 10.0f;

        // 🔴 POPUPROUNDING DOES NOT AFFECT MODALS. It governs tooltips and
        // dropdowns only.
        //
        // The comment that used to sit here said the opposite - that setting it
        // was what stopped modals having square corners - and it was wrong.
        // imgui.cpp:7763 picks the radius as ChildWindow -> ChildRounding,
        // Popup AND NOT Modal -> PopupRounding, everything else ->
        // WindowRounding. A modal carries both the Popup and Modal flags, so it
        // is excluded from the PopupRounding branch entirely and falls through
        // to WindowRounding. Setting this value rounded the tooltips and left
        // every modal square, which is exactly what was observed and then
        // misdiagnosed. Modals are handled per-dialog by OptiMaxUi::ModalStyle;
        // the reasoning lives there.
        //
        // The old text also carried two figures that are no longer true: it
        // described cards as rounded at 10 (ChildRounding is 18 above) and
        // argued 8 should sit BELOW the card radius, which is the opposite of
        // the conclusion the owner reached on screen for modals.
        //
        // ⚠️ There were also TWO assignments to this, 12 then 8, with only the
        // second taking effect. One remains.
        //
        // 8 is kept for tooltips and dropdowns on its own merits - they are
        // small, and ImGui clamps rounding to half the shorter side, so a
        // larger radius would turn a one-line tooltip into a lozenge.
        style.PopupRounding = 8.0f;

        // Global border OFF, because the main window is fullscreen and
        // borderless and this would outline the whole application.
        // ModalStyle pushes it back to 1 per dialog.
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

        // THE FIX: the eight overrides above were the whole palette, so every other
        // widget kept StyleColorsDark()'s accent - which is ImGui's signature blue
        // (0.26, 0.59, 0.98). Nothing showed it while the UI was hand-drawn cards and
        // Selectables (Header* is overridden, so those looked right), but the moment a
        // real dropdown and a popup menu were added the blue turned up as a solid
        // FrameBg block, a blue focus rectangle and blue check marks sitting inside a
        // purple app. Accent the rest against the same three purples used above rather
        // than leaving two themes fighting.
        const ImVec4 accent = ImVec4(0.40f, 0.15f, 0.80f, 1.00f);  // base
        const ImVec4 accentBright = ImVec4(0.55f, 0.25f, 0.95f, 1.00f);  // hover
        const ImVec4 accentHot = ImVec4(0.65f, 0.35f, 1.00f, 1.00f);  // active

        // Dropdowns, text inputs, checkboxes, sliders - FrameBg is the blue block.
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.06f, 0.16f, 0.60f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(accentBright.x, accentBright.y, accentBright.z, 0.60f);
        style.Colors[ImGuiCol_CheckMark] = accentHot;
        style.Colors[ImGuiCol_SliderGrab] = accentBright;
        style.Colors[ImGuiCol_SliderGrabActive] = accentHot;

        // Only affects buttons that don't push their own colours - the custom ones
        // (PremiumButton, the modal pairs) still override locally and are untouched.
        style.Colors[ImGuiCol_Button] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(accentBright.x, accentBright.y, accentBright.z, 0.65f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(accentHot.x, accentHot.y, accentHot.z, 0.85f);

        style.Colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.15f, 0.50f, 0.50f);
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(accentBright.x, accentBright.y, accentBright.z, 0.70f);
        style.Colors[ImGuiCol_SeparatorActive] = accentHot;

        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.02f, 0.08f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.07f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.02f, 0.08f, 0.75f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.05f, 0.02f, 0.08f, 1.00f);

        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.03f, 0.40f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.14f, 0.45f, 0.80f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.20f, 0.65f, 0.90f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = accentBright;

        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(accentBright.x, accentBright.y, accentBright.z, 0.60f);
        style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(accentHot.x, accentHot.y, accentHot.z, 0.90f);

        // Names here are the current ImGui ones - TabSelected/TabDimmed*, not the old
        // TabActive/TabUnfocused*, which no longer exist in this version.
        style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.07f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(accentBright.x, accentBright.y, accentBright.z, 0.70f);
        style.Colors[ImGuiCol_TabSelected] = ImVec4(0.35f, 0.14f, 0.62f, 1.00f);
        style.Colors[ImGuiCol_TabSelectedOverline] = accentHot;
        style.Colors[ImGuiCol_TabDimmed] = ImVec4(0.10f, 0.05f, 0.16f, 1.00f);
        style.Colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.22f, 0.10f, 0.38f, 1.00f);
        style.Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(accent.x, accent.y, accent.z, 0.60f);

        style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.06f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.30f, 0.15f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.18f, 0.10f, 0.28f, 1.00f);

        // ProgressBar draws its fill with PlotHistogram. Both existing bars already push
        // their own purple locally (`:1537` fadedPurple, `:2725` sexyPurple, which is this
        // same accentHot), so nothing on screen was orange - but that made those two pushes
        // load-bearing, and the next ProgressBar added without one would have rendered in
        // ImGui's default orange. Exactly the A4-18 shape: a palette gap that stays
        // invisible until a new widget lands on it. Make the default correct instead.
        style.Colors[ImGuiCol_PlotHistogram] = accentHot;
        style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.78f, 0.55f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_PlotLines] = accentBright;
        style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.78f, 0.55f, 1.00f, 1.00f);

        style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        style.Colors[ImGuiCol_TextLink] = accentHot;
        // The blue rectangle drawn around the focused item when a popup opens - the
        // most obvious offender in the new menus, since popups take nav focus on open.
        style.Colors[ImGuiCol_NavCursor] = accentHot;
        style.Colors[ImGuiCol_DragDropTarget] = ImVec4(accentHot.x, accentHot.y, accentHot.z, 0.90f);

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

        // THE FIX: this was decided by substring-matching the CPU's marketing name -
        // Intel plus any of "12"/"13"/"14"/"ultra", or Ryzen plus "x3d"/"7900"/"7950".
        // It was wrong in both directions, and it gates the whole P-Core Affinity Lock
        // card:
        //
        //   - The AMD half names no hybrid CPU at all. A 7900X/7950X is uniform Zen 4,
        //     and the X3D parts differ in CACHE, not core type - Windows reports one
        //     EfficiencyClass for every core on all of them. So the feature was offered,
        //     and applied, on machines that have no performance/efficiency split.
        //   - The Intel half matches a bare "12"/"13"/"14" anywhere in the string, so an
        //     old Xeon E5-1428L reads as hybrid, while genuinely hybrid parts the list
        //     predates (AMD's Zen5/Zen5c mobile chips) read as not.
        //   - Non-hybrid 12th-13th gen parts exist too - an i3-12100 has zero E-cores -
        //     and every one of them matched.
        //
        // Windows already answers this exactly, for any vendor and any part that ships
        // after today: GetLogicalProcessorInformationEx reports an EfficiencyClass per
        // core, and more than one distinct value IS what "hybrid" means.
        // BuildPerformanceCoreMask returns 0 when they are all the same, which is the
        // same test LockToPCores itself now makes before touching anything - so the UI
        // and the tweak can no longer disagree about whether this CPU has P-cores.
        isHeterogeneousCPU = (OptiMax::V3::BuildPerformanceCoreMask() != 0);

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
        // A sexyPink (1.0, 0.35, 0.78) lived here briefly for bullet markers on the
        // changelog and the session report. Removed rather than left unreferenced:
        // both panels already carry the accent in their outline, and a second accent
        // beside it competed with the first instead of reinforcing it. Noted because
        // "add a pink" is a reasonable thing to try again, and this is the reason it
        // did not work - not that the colour was wrong.
        ImGuiWindowFlags staticCardFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        while (!done) {
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT) done = true;
            }
            if (done) break;

            // The updater has swapped the binary and started the new process; it
            // asks for the shutdown here rather than calling exit() from its own
            // detached thread. Checked before the tray-idle branch below, because a
            // tray-resident instance must still exit or the incoming process sits on
            // the single-instance mutex until it times out.
            if (g_updateRestartRequested.load()) {
                SafeAddLog("UPDATER: Handing over to the new version - closing this one.");
                done = true;
                break;
            }

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
                {
                    OptiMaxUi::ModalStyle _modalStyle;
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

                    // Two rows, derived rather than the old 90px constant - which was
                    // ~3px short of the content once the base font grew, so the second
                    // row lost its descenders. staticCardFlags means no scrollbar, so
                    // the shortfall showed as clipped text rather than as a scroll.
                    ImGui::BeginChild("VersionBox",
                        ImVec2(0, 15 * dpiScale
                            + ImGui::GetTextLineHeight() * 2.0f
                            + ImGui::GetStyle().ItemSpacing.y * 2.0f
                            + 15 * dpiScale),
                        true, staticCardFlags);
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
                }

                // THE FEATURE: the X button no longer closes immediately - WM_CLOSE
                // just sets this flag instead of destroying the window (see WndProc).
                // This dialog is the only thing that actually decides what happens.
                if (g_showCloseConfirm.load()) {
                    ImGui::OpenPopup("Close OptiMax?");
                    g_showCloseConfirm = false;
                }

                ImGui::SetNextWindowSize(ImVec2(420 * dpiScale, 0));
                {
                    OptiMaxUi::ModalStyle _modalStyle;
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
                {
                    OptiMaxUi::ModalStyle _modalStyle;
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
                }

                // ==============================================================
                // WHAT'S NEW - shown once per version, after an update.
                //
                // Gated on no game running for the same reason the latency test
                // and Clear Caches are: a modal appearing over someone's
                // fullscreen match is the tool interrupting the thing it exists
                // to protect. Deferring costs nothing because g_lastSeenVersion
                // is only written when the prompt is dismissed - leave it unset
                // and this simply comes back next launch.
                //
                // Also held back while the update prompt or the sign-in flow is
                // up, so two modals can never fight over the same frame.
                if (g_showChangelogPrompt.load() && !g_isGameRunning.load()
                    // The cast is required: IsPopupOpen is overloaded on
                    // const char* and ImGuiID, and a bare NULL matches both.
                    && !g_isDownloadingUpdate.load()
                    && !ImGui::IsPopupOpen(static_cast<const char*>(nullptr), ImGuiPopupFlags_AnyPopupId)) {
                    ImGui::OpenPopup("What's New");
                    g_showChangelogPrompt = false;
                }

                ImGui::SetNextWindowSize(ImVec2(500 * dpiScale, 0));
                // This dialog was the first to get the rounded/bordered treatment
                // and carried the explanation inline. That reasoning - why
                // WindowRounding and not PopupRounding, why per-dialog and not
                // global, why radius 10 - now lives on OptiMaxUi::ModalStyle,
                // which is where the next person looks. All eight modals use it.
                {
                    OptiMaxUi::ModalStyle _modalStyle;
                if (ImGui::BeginPopupModal("What's New", NULL,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {

                    if (m_fontBold) ImGui::PushFont(m_fontBold);
                    ImGui::TextColored(sexyPurple, "What's New in v%s", OPTIMAX_VERSION_STRING);
                    if (m_fontBold) ImGui::PopFont();

                    // The underline stroke that was here is gone. A dashboard card
                    // heads itself with bold purple text and nothing else, so a
                    // pink rule under this one made it the odd panel out - which
                    // was the opposite of the intent.
                    ImGui::Spacing();

                    for (const auto& line : OptiMax::Changelog::Entries()) {
                        // Plain ImGuiCol_Text markers. These were pink and it was
                        // too much on a panel that is already outlined in the
                        // accent colour - the outline is doing that job, and a
                        // second accent next to it competes with it rather than
                        // reinforcing it.
                        ImGui::Bullet();
                        ImGui::SameLine();
                        ImGui::TextWrapped("%s", line.c_str());
                        ImGui::Spacing();
                    }

                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    if (PremiumButton("OK", ImVec2(-1, globalBtnHeight * 1.3f))) {
                        // Recorded HERE and only here. Writing it at startup
                        // would mean a crash, a close, or a game starting before
                        // the prompt appeared silently consumed the one chance
                        // the user had to see it.
                        {
                            std::lock_guard<std::mutex> lock(g_configMutex);
                            g_lastSeenVersion = OPTIMAX_VERSION_STRING;
                        }
                        SavePersistentState();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                }   // ModalStyle - the pop is the destructor, and it has to happen
                    // outside the if: the push runs every frame, BeginPopupModal
                    // only returns true on the frames the dialog is open.

                if (g_isDownloadingUpdate.load()) { ImGui::OpenPopup("Downloading Update..."); }

                {
                    OptiMaxUi::ModalStyle _modalStyle;
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
                }

                // --- SIDEBAR MENU ---
                ImGui::BeginChild("Sidebar", ImVec2(280 * dpiScale, 0), true);
                ImGui::SetCursorPosY(25.0f * dpiScale);

                // The wordmark is split: OPTI in white, MAX in the accent. It was
                // one TextColored call in purple, which read as a label rather
                // than a logo - the split gives it a fixed point for the eye
                // without adding an asset.
                //
                // SameLine(0, 0) is load-bearing: the default spacing argument
                // would leave ItemSpacing.x between the halves and break the word
                // in two. Centring measures the whole string first, because
                // measuring the halves separately and summing them is what makes
                // split wordmarks drift off-centre.
                if (m_fontBold) ImGui::PushFont(m_fontBold);
                {
                    const float sidebarWidth = ImGui::GetWindowSize().x;
                    const float textWidth = ImGui::CalcTextSize("OPTIMAX PRO").x;
                    ImGui::SetCursorPosX((sidebarWidth - textWidth) * 0.5f);
                    ImGui::TextUnformatted("OPTI");
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextColored(sexyPurple, "MAX");
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextColored(ImVec4(0.55f, 0.52f, 0.62f, 1.0f), " PRO");
                }
                if (m_fontBold) ImGui::PopFont();

                ImGui::SetCursorPosY(65.0f * dpiScale); ImGui::Separator(); ImGui::SetCursorPosY(80.0f * dpiScale);

                // ---- NAVIGATION: five rows, flat ----
                // Was four collapsible categories over eleven destinations. The
                // categories were not the problem - the destinations were, and
                // three of the four categories held pages that belonged together
                // anyway. With eleven merged down to six there is nothing left to
                // group, and a category header that never collapses anything is
                // just a row that cannot be clicked.
                //
                // Settings stays anchored at the bottom, below the account row,
                // where it already was.
                {
                    const float navRowH = ImGui::GetTextLineHeight() + 16.0f * dpiScale;
                    // 🔴 THE ICON GETS A FIXED COLUMN, AND THE LABEL A FIXED X.
                    //
                    // The first version built one string - "  <icon>   <label>" -
                    // and handed it to Selectable. Every icon then contributed its
                    // own advance width, so a narrow bolt and a wide cloud pushed
                    // their labels to different x positions and the rail read as
                    // ragged. No amount of padding in the format string fixes that;
                    // the glyphs are simply different widths.
                    //
                    // So: an empty Selectable owns the hit box and the highlight,
                    // and both the icon and the label are drawn into it at
                    // positions this code chooses. DrawIconCentered centres the
                    // glyph's real ink in the column, which is the same reason the
                    // KPI and toggle-row icons needed it.
                    const float navIconCol = 26.0f * dpiScale;
                    const float navPadL = 10.0f * dpiScale;
                    auto navRow = [&](const char* icon, const char* label, int tab) {
                        ImGui::PushID(tab);
                        const bool sel = (activeTab == tab);
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        if (ImGui::Selectable("##nav", sel, 0, ImVec2(0, navRowH))) activeTab = tab;
                        const bool hov = ImGui::IsItemHovered();

                        // Selected rows get the accent as text rather than as a
                        // filled bar: purple is the signal colour in this UI, and
                        // a filled row would make the rail the loudest thing on
                        // screen instead of the page content.
                        const ImU32 col = sel ? OptiMaxUi::kAccentHot
                            : (hov ? OptiMaxUi::kInk : OptiMaxUi::kDim);
                        ImDrawList* ndl = ImGui::GetWindowDrawList();
                        OptiMaxUi::DrawIconCentered(ndl,
                            ImVec2(p.x + navPadL, p.y),
                            ImVec2(p.x + navPadL + navIconCol, p.y + navRowH), icon, col);
                        const ImVec2 ls = ImGui::CalcTextSize(label);
                        ndl->AddText(ImVec2(p.x + navPadL + navIconCol + 10.0f * dpiScale,
                            p.y + (navRowH - ls.y) * 0.5f), col, label);
                        ImGui::PopID();
                        };

                    navRow(OMX_ICON_GRID, "Dashboard", TAB_DASHBOARD);
                    navRow(OMX_ICON_BOLT, "Tweaks", TAB_TWEAKS);
                    navRow(OMX_ICON_PLAY, "Games", TAB_GAMES);
                    navRow(OMX_ICON_TIMER, "Diagnostics", TAB_DIAGNOSTICS);
                    navRow(OMX_ICON_CLOUD, "Discord", TAB_DISCORD);
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

                // Same fixed icon column as the rail above, so Settings lines up
                // with it instead of being the one row that does not.
                ImGui::SetCursorPos(ImVec2(6.0f * dpiScale, sidebarH - (75.0f * dpiScale)));
                {
                    const bool selSettings = (activeTab == TAB_SETTINGS);
                    const float navRowH = ImGui::GetTextLineHeight() + 16.0f * dpiScale;
                    const float navIconCol = 26.0f * dpiScale;
                    const float navPadL = 10.0f * dpiScale;
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##navsettings", selSettings, 0, ImVec2(0, navRowH))) activeTab = TAB_SETTINGS;
                    const bool hov = ImGui::IsItemHovered();
                    const ImU32 col = selSettings ? OptiMaxUi::kAccentHot
                        : (hov ? OptiMaxUi::kInk : OptiMaxUi::kDim);
                    ImDrawList* ndl = ImGui::GetWindowDrawList();
                    OptiMaxUi::DrawIconCentered(ndl,
                        ImVec2(p.x + navPadL, p.y),
                        ImVec2(p.x + navPadL + navIconCol, p.y + navRowH), OMX_ICON_GEAR, col);
                    const ImVec2 ls = ImGui::CalcTextSize("Settings");
                    ndl->AddText(ImVec2(p.x + navPadL + navIconCol + 10.0f * dpiScale,
                        p.y + (navRowH - ls.y) * 0.5f), col, "Settings");
                }

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
                {
                    OptiMaxUi::ModalStyle _modalStyle;
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
                }

                // --- MAIN CONTENT AREA ---
                ImGui::BeginChild("Main", ImVec2(0, 0), false);
                ImGui::SetCursorPosY(25.0f * dpiScale);

                if (m_fontBold) ImGui::PushFont(m_fontBold);
                // The title names the destination only. What used to distinguish
                // eleven titles now lives in the sub-tab bar below, which is the
                // thing the user actually clicked.
                if (activeTab == TAB_DASHBOARD)        ImGui::TextColored(sexyPurple, "  Dashboard");
                else if (activeTab == TAB_TWEAKS)      ImGui::TextColored(sexyPurple, "  Tweaks");
                else if (activeTab == TAB_GAMES)       ImGui::TextColored(sexyPurple, "  Games");
                else if (activeTab == TAB_DIAGNOSTICS) ImGui::TextColored(sexyPurple, "  Diagnostics");
                else if (activeTab == TAB_DISCORD)     ImGui::TextColored(sexyPurple, "  Discord Debloat");
                else if (activeTab == TAB_SETTINGS)    ImGui::TextColored(sexyPurple, "  Settings");
                if (m_fontBold) ImGui::PopFont();

                // ==============================================================
                // TOP BAR (v4.3.0 UI pass)
                //
                // Right-hand side of the page-title row: what the machine is doing
                // right now, then the two actions worth reaching from any page.
                //
                // The game chip is the piece that earns its place - it answers
                // "is OptiMax actually doing anything" without navigating, which
                // is the question a user opens this app to ask. Laid out right to
                // left from the content region so it stays anchored when the
                // window resizes, rather than from a fixed offset.
                // ==============================================================
                {
                    using namespace OptiMaxUi;
                    const bool gameOn = g_isGameRunning.load();
                    std::string gameChip;
                    if (gameOn) {
                        const std::wstring wexe = GetActiveGameExe();
                        // Rendered on the dashboard, so a mangled name here is
                        // the user watching their own game's title come out as
                        // garbage. ImGui renders UTF-8 natively.
                        std::string exe = OptiMax::Text::ToUtf8(wexe);
                        const size_t dot = exe.find_last_of('.');
                        if (dot != std::string::npos) exe = exe.substr(0, dot);
                        gameChip = exe.empty() ? "GAME DETECTED" : (exe + "  -  boosted");
                    }
                    else {
                        gameChip = "no game running";
                    }

                    const char* bCleanRam = "Clean RAM";
                    const char* bCore = g_isCoreActive.load() ? "Revert core tweaks" : "Apply core tweaks";
                    const float wChip = ChipWidth(gameChip.c_str());
                    const float wRam = ImGui::CalcTextSize(bCleanRam).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    const float wCore = ImGui::CalcTextSize(bCore).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    const float sp = ImGui::GetStyle().ItemSpacing.x;
                    const float need = wChip + wRam + wCore + sp * 3.0f;

                    ImGui::SameLine();
                    const float avail = ImGui::GetContentRegionAvail().x;
                    // Only drawn when it genuinely fits. A top bar that wraps onto
                    // a second line looks worse than no top bar, and at a narrow
                    // window the sidebar already shows this state.
                    if (avail > need + 12.0f) {
                        ImGui::Dummy(ImVec2(avail - need, 1)); ImGui::SameLine();

                        const ImVec2 cp = ImGui::GetCursorScreenPos();
                        DrawChip(ImGui::GetWindowDrawList(),
                            ImVec2(cp.x, cp.y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight() - 6.0f) * 0.5f),
                            gameChip.c_str(), gameOn ? ChipKind::On : ChipKind::Off);
                        ImGui::Dummy(ImVec2(wChip, ImGui::GetFrameHeight()));

                        ImGui::SameLine();
                        if (ImGui::Button(bCleanRam, ImVec2(wRam, 0))) {
                            const int n = OptiMax::SmartBackgroundPurge(GetActiveGameExe());
                            SafeAddLog("RAM: Trimmed " + std::to_string(n) +
                                " background app(s) on request (the running game was left untouched).");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(bCore, ImVec2(wCore, 0))) {
                            if (g_isCoreActive.load()) RevertCoreTweaksNow();
                            else ApplyCoreTweaksNow(localAntiCheatActive);
                        }
                    }
                }

                ImGui::SetCursorPosY(65.0f * dpiScale); ImGui::Separator(); ImGui::SetCursorPosY(80.0f * dpiScale);

                // ---- SUB-TABS ----
                // The two destinations that carry more than one view. These are
                // views of one subject rather than separate places, which is why
                // they are tabs and not five more rows in the rail.
                if (activeTab == TAB_GAMES) {
                    if (ImGui::BeginTabBar("GamesTabs")) {
                        // SetSelected only on the frame the Dashboard jump asked
                        // for it - passing it every frame would pin the bar and
                        // the other tab could never be opened.
                        const ImGuiTabItemFlags libFlag = gamesForceLibrary
                            ? ImGuiTabItemFlags_SetSelected : 0;
                        gamesForceLibrary = false;
                        if (ImGui::BeginTabItem("My Games", nullptr, libFlag)) { gamesSubTab = 0; ImGui::EndTabItem(); }
                        if (ImGui::BeginTabItem("Community Profiles")) { gamesSubTab = 1; ImGui::EndTabItem(); }
                        ImGui::EndTabBar();
                    }
                    ImGui::Dummy(ImVec2(0, 4 * dpiScale));
                }
                else if (activeTab == TAB_DIAGNOSTICS) {
                    // Live Monitoring leads: it is the page that says something the
                    // moment you open it, whereas the latency test is a 60-second
                    // deliberate action that refuses to run while a game is on. The
                    // tab you land on should be the one that already has an answer.
                    if (ImGui::BeginTabBar("DiagTabs")) {
                        if (ImGui::BeginTabItem("Live Monitoring")) { diagSubTab = 1; ImGui::EndTabItem(); }
                        if (ImGui::BeginTabItem("Latency Test")) { diagSubTab = 0; ImGui::EndTabItem(); }
                        if (ImGui::BeginTabItem("Logs")) { diagSubTab = 2; ImGui::EndTabItem(); }
                        ImGui::EndTabBar();
                    }
                    ImGui::Dummy(ImVec2(0, 4 * dpiScale));
                }

                // Set by the throttling row on EITHER the Dashboard or the Tweaks
                // page, consumed once below where the modal is declared. A flag
                // rather than calling OpenPopup from inside the row: ImGui derives
                // a popup's id from the CURRENT window's id stack, so opening it
                // from within a child and declaring it at page level produces two
                // different ids - which is why this dialog may never have worked
                // before it was routed this way. Declared here, above the page
                // dispatch, because two pages now raise it.
                bool wantThrottleConfirm = false;

                if (activeTab == TAB_DASHBOARD) {
                    // Banner heights: 16 padding + 24 title + 12 spacing + two 20px text
                    // lines + 16 padding = 108. The old 70px clipped the bottom of even a
                    // single-line message, and these messages are now two lines.
                    // Derived, not the old 115px constant. That number was summed by
                    // hand against the 20px base font ("16 padding + 24 title + 12
                    // spacing + two 20px lines + 16 padding = 108") and the bigger
                    // fonts pushed the wrapped text past it - and these cards use
                    // staticCardFlags, which disables scrolling, so the overflow is
                    // silently clipped rather than reachable. Three body lines of
                    // headroom because the text wraps and the count is not fixed.
                    float bannerTitleH = ImGui::GetTextLineHeight();
                    if (m_fontBold) {
                        ImGui::PushFont(m_fontBold);
                        bannerTitleH = ImGui::GetTextLineHeight();
                        ImGui::PopFont();
                    }
                    const float bannerHeight =
                        ImGui::GetStyle().WindowPadding.y * 2.0f
                        + bannerTitleH
                        + ImGui::GetStyle().ItemSpacing.y
                        + ImGui::GetTextLineHeight() * 3.0f;

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

                    // ==========================================================
                    // SESSION OVERVIEW (v4.3.0 UI pass)
                    //
                    // Five KPI tiles with sparklines, then the stall timeline and
                    // the optimization ring. Placed above the existing cards
                    // rather than replacing them: the hardware config and the
                    // rest still have a job, they just are not the first thing
                    // you should see.
                    //
                    // Every figure here is one OptiMax genuinely knows. There is
                    // deliberately no composite "health score" - it would have to
                    // be invented, and this app's whole discipline is not claiming
                    // what it has not measured.
                    // ==========================================================
                    {
                        using namespace OptiMaxUi;
                        // First frame this renders is close enough to session
                        // start for the axis; the alternative is threading a real
                        // start time through, for a label.
                        static const ULONGLONG sessionStartUtc =
                            OptiMax::Diagnostics::Attribution::NowUtc();

                        Eyebrow("THIS SESSION");

                        float h[60]; int hn = 0;
                        const float gap = ImGui::GetStyle().ItemSpacing.x;
                        const float tileW = (ImGui::GetContentRegionAvail().x - gap * 4.0f) / 5.0f;

                        // 🔴 DERIVED, NOT THE OLD 148px CONSTANT. That constant was
                        // sized against the 20px base font; at 23px base and 42px
                        // numerals the text block outgrew it, the overflow guard
                        // inside KpiTile pulled the text up to compensate, and the
                        // sparkline lost the room it needed - which is half of why
                        // the graphs became unreadable. Summing the parts means the
                        // next font change cannot repeat it.
                        const float kpiLine = ImGui::GetTextLineHeight();
                        float kpiNum = kpiLine;
                        if (m_fontNumber) {
                            ImGui::PushFont(m_fontNumber);
                            kpiNum = ImGui::GetTextLineHeight();
                            ImGui::PopFont();
                        }
                        const float tileH =
                            14.0f                    // top padding
                            + kpiLine + 2.0f         // label
                            + kpiNum + 2.0f          // the big value
                            + kpiLine                // delta line
                            + 10.0f                  // breathing room above the plot
                            + 52.0f * dpiScale;      // the sparkline band
                        const ImVec2 tileSz(tileW, tileH);

                        char buf[32];

                        // Logical processor count, read once. GetSystemInfo is
                        // cheap but there is no reason to call it every frame.
                        static char coreTxt[24] = { 0 };
                        if (!coreTxt[0]) {
                            SYSTEM_INFO si{}; GetSystemInfo(&si);
                            sprintf_s(coreTxt, "%lu cores", si.dwNumberOfProcessors);
                        }

                        histCpu.Snapshot(h, hn);
                        sprintf_s(buf, "%d", cachedCpuLoad);
                        KpiTile(OMX_ICON_CPU, "CPU load", buf, "%", coreTxt, kDimmer,
                            h, hn, 0.0f, tileSz, m_fontNumber, false, kSigCpu);
                        ImGui::SameLine();

                        histRam.Snapshot(h, hn);
                        sprintf_s(buf, "%d", cachedRam);
                        // The headline is the percentage in use; the sub-line says
                        // what that leaves, which is the number people act on.
                        char freeTxt[32];
                        sprintf_s(freeTxt, "%d%% still free", 100 - ImClamp(cachedRam, 0, 100));
                        KpiTile(OMX_ICON_MEM, "Memory", buf, "%", freeTxt, kCalm,
                            h, hn, 0.0f, tileSz, m_fontNumber, false, kSigMem);
                        ImGui::SameLine();

                        histDpc.Snapshot(h, hn);
                        sprintf_s(buf, "%.0f", cachedDpcSpike);
                        KpiTile(OMX_ICON_CLOCK, "DPC latency", buf, OMX_MICRO "s",
                            cachedDpcSpike < 500.0 ? "consistent" : "spiky",
                            cachedDpcSpike < 500.0 ? kCalm : kWarn,
                            h, hn, 0.0f, tileSz, m_fontNumber, false, kSigDpc);
                        ImGui::SameLine();

                        sprintf_s(buf, "%.2f", cachedTimer);
                        KpiTile(OMX_ICON_TIMER, "System timer", buf, "ms",
                            (cachedTimer > 0.0 && cachedTimer <= 0.6) ? "verified active" : "at OS default",
                            (cachedTimer > 0.0 && cachedTimer <= 0.6) ? kCalm : kDim,
                            nullptr, 0, 1.0f, tileSz, m_fontNumber);
                        ImGui::SameLine();

                        const int stallsNow = OptiMax::Diagnostics::g_stallCount.load();
                        const unsigned long long worstNow = OptiMax::Diagnostics::g_worstStallMs.load();
                        sprintf_s(buf, "%d", stallsNow);
                        char worstTxt[40];
                        if (stallsNow > 0) sprintf_s(worstTxt, "worst %llums", worstNow);
                        else sprintf_s(worstTxt, "none detected");
                        KpiTile(OMX_ICON_WARN, "Stalls detected", buf, "", worstTxt,
                            stallsNow > 0 ? kWarn : kCalm,
                            nullptr, 0, 1.0f, tileSz, m_fontNumber, stallsNow > 0);

                        ImGui::Dummy(ImVec2(0, 6 * dpiScale));

                        // ---- timeline + ring, side by side ----
                        // Height derived from what goes in it rather than a picked
                        // number. The first attempt used a flat 258px, which was
                        // tuned against the mockup's smaller type - at this app's
                        // 20px base the "Copy report" button fell off the bottom of
                        // one card and the fifth legend row off the other.
                        // 🔴 ItemSpacing.y BETWEEN EVERY ITEM, and forgetting it is
                        // what kept these cards clipped even after the heights were
                        // computed from font metrics. ImGui inserts ItemSpacing.y
                        // after each submitted item, so a card with seven stacked
                        // items is six gaps taller than the sum of its contents -
                        // 72px at the default spacing, which is exactly the amount
                        // the "Copy report" button was falling short by.
                        const float lineH = ImGui::GetTextLineHeight();
                        const float spY = ImGui::GetStyle().ItemSpacing.y;
                        const float plotH = 74.0f * dpiScale;
                        const float sessionCardH =
                            ImGui::GetStyle().WindowPadding.y * 2.0f   // card padding
                            + lineH                                    // title
                            + (lineH + 8.0f)                           // chip row
                            + lineH                                    // subtitle
                            + plotH + 6.0f                             // plot
                            + lineH                                    // axis labels
                            + globalBtnHeight                          // button
                            + spY * 7.0f;                              // the gaps between them
                        if (ImGui::BeginTable("SessionGrid", 2,
                            ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp)) {
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::BeginChild("StallCard", ImVec2(0, sessionCardH), true, staticCardFlags);
                            {
                                // Title left, state chip hard right - the mockup's
                                // header row. Measured from the content region
                                // rather than a fixed offset, so it stays put when
                                // the window is resized.
                                if (m_fontBold) ImGui::PushFont(m_fontBold);
                                ImGui::TextColored(sexyPurple, "What held your machine up");
                                if (m_fontBold) ImGui::PopFont();
                                {
                                    char chipTxt[40];
                                    if (stallsNow > 0) sprintf_s(chipTxt, "WORST %llums", worstNow);
                                    else sprintf_s(chipTxt, "NO STALLS");
                                    const float cw = ChipWidth(chipTxt);
                                    ImGui::SameLine();
                                    const float avail = ImGui::GetContentRegionAvail().x;
                                    ImGui::Dummy(ImVec2(ImMax(0.0f, avail - cw), 1));
                                    ImGui::SameLine();
                                    DrawChip(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
                                        chipTxt, stallsNow > 0 ? ChipKind::Pending : ChipKind::On);
                                    ImGui::Dummy(ImVec2(cw, ImGui::GetTextLineHeight() + 6.0f));
                                }
                                const unsigned long long wd = OptiMax::Diagnostics::g_watchdogChecks.load();
                                if (wd == 0) {
                                    // Never a verdict without evidence something was
                                    // watching - the whole reason g_watchdogChecks exists.
                                    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                                        "The stall watchdog is not running - detection is unavailable.");
                                }
                                else {
                                    ImGui::TextDisabled("%d stall%s  -  %llu watchdog checks  -  hover a spike",
                                        stallsNow, stallsNow == 1 ? "" : "s", wd);
                                }
                                ImGui::Dummy(ImVec2(0, 4));

                                std::vector<StallMark> marks;
                                for (const auto& r : OptiMax::Diagnostics::SnapshotStalls()) {
                                    StallMark m;
                                    m.whenUtc = r.whenUtc;
                                    m.lateMs = r.lateMs;
                                    m.timeText = OptiMax::Diagnostics::FormatLocalTime(r.whenUtc);
                                    m.cause = r.cause;
                                    // "unattributed" and "not determined" both mean no
                                    // cause was named; only a real finding gets the
                                    // warning colour.
                                    m.attributed = (r.cause.find("unattributed") == std::string::npos &&
                                        r.cause.find("not determined") == std::string::npos);
                                    marks.push_back(m);
                                }
                                const ULONGLONG nowUtc = OptiMax::Diagnostics::Attribution::NowUtc();
                                const std::string t0 = OptiMax::Diagnostics::FormatLocalTime(sessionStartUtc);
                                const std::string t1 = OptiMax::Diagnostics::FormatLocalTime(nowUtc);
                                const char* axis[2] = { t0.c_str(), t1.c_str() };
                                StallTimeline(marks, sessionStartUtc, nowUtc, axis, 2, plotH);

                                if (PremiumButton("Copy report for support", ImVec2(-1, globalBtnHeight))) {
                                    ImGui::SetClipboardText(
                                        OptiMax::Diagnostics::BuildSessionReport(MakeReportContext()).c_str());
                                    SafeAddLog("DIAGNOSTICS: Session report copied to the clipboard.");
                                }
                            }
                            ImGui::EndChild();

                            ImGui::TableSetColumnIndex(1);
                            ImGui::BeginChild("OptRing", ImVec2(0, sessionCardH), true, staticCardFlags);
                            {
                                if (m_fontBold) ImGui::PushFont(m_fontBold);
                                ImGui::TextColored(sexyPurple, "Optimizations");
                                if (m_fontBold) ImGui::PopFont();
                                ImGui::TextDisabled("Every one of these can be turned back off.");
                                ImGui::Dummy(ImVec2(0, 6));

                                // 🔴 ONE LIST FEEDS BOTH THE RING AND THE LEGEND.
                                //
                                // They used to be two separate lists and they did not
                                // agree. The ring counted nine flags; the legend named
                                // five, only three of which were among those nine.
                                // "Boot" was in the legend and NOT in the ring's set,
                                // and "Menu delays" passed a hardcoded false so it
                                // never read g_snappyUI at all - it would have said
                                // "Off" on a Windows 11 machine with the tweak on.
                                //
                                // The visible symptom was the ring reading 100% beside
                                // a legend showing a Reboot and an N/A, which is not a
                                // styling problem and could not have been styled away.
                                // Same discipline as the Tweaks page: derive both from
                                // the one array so they cannot drift.
                                struct OptRow { const char* name; bool on; bool pending; };
                                const OptRow optRows[] = {
                                    { "Timing",       g_isCoreActive.load(),       false },
                                    { "Auto-boost",   g_autoBoostEnabled.load(),   false },
                                    { "Throttling",   g_isolationActive.load(),    false },
                                    { "Core parking", g_coresUnparked.load(),      false },
                                    { "Network",      g_networkTweaked.load(),     false },
                                    { "Mouse input",  g_mouseAccelDisabled.load(), false },
                                    { "Fullscreen",   g_fseForced.load(),          false },
                                    { "MSI mode",     g_msiEnabled.load(),         true  },
                                    { "Global timer", g_globalTimerRes.load(),     true  },
                                };
                                const int optTotal = (int)IM_ARRAYSIZE(optRows);
                                int on = 0; for (const OptRow& r : optRows) if (r.on) ++on;

                                // A percentage, not "9/9". The fraction made you do
                                // the arithmetic and read as a score out of nine
                                // rather than a state; the ring already shows the
                                // proportion visually, so the number should match it.
                                //
                                // NOTHING ELSE GOES INSIDE THE RING - the legend
                                // beside it now genuinely does name all nine.
                                char ringVal[16]; sprintf_s(ringVal, "%d", (on * 100) / optTotal);
                                const char* ringUnit = "%";

                                // THREE columns: dial, then the legend SPLIT IN TWO.
                                //
                                // At one full-width legend column, "Timing" and its
                                // "On" sat at opposite ends of ~270px of nothing -
                                // the value has to be right-aligned for the states to
                                // scan down as a column, so a wide row is mostly gap.
                                // This is the same problem the CONTROLS block below
                                // has, and it takes the same fix: halve the width.
                                // Splitting nine rows 5/4 also fills the height this
                                // card was leaving empty, since it has to match the
                                // stall timeline's height and had far less in it.
                                // State colours, shared by the ring's arcs and the
                                // legend's words so the two read as one control.
                                auto stateText = [](const OptRow& r) {
                                    // "Reboot" rather than "On" for a setting that is
                                    // saved but only read at boot - a two-state
                                    // readout would claim it is in effect when it is
                                    // not.
                                    return r.on ? (r.pending ? "Reboot" : "On") : "Off";
                                    };
                                auto stateU32 = [](const OptRow& r) {
                                    return r.on ? (r.pending ? OptiMaxUi::kWarn : OptiMaxUi::kAccentBr)
                                        : OptiMaxUi::kLine;
                                    };

                                // The legend text sits below the body size. It is a
                                // dense secondary readout, and at full size "Global
                                // timer" was wide enough to collide with its own
                                // value in a half-width column.
                                const float legFont = ImGui::GetStyle().FontSizeBase * 0.86f;

                                if (ImGui::BeginTable("RingSplit", 3,
                                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
                                    const float dialR = 62.0f * dpiScale;
                                    ImGui::TableSetupColumn("dial", ImGuiTableColumnFlags_WidthFixed, dialR * 2.0f + 10.0f);
                                    ImGui::TableSetupColumn("legA", ImGuiTableColumnFlags_WidthStretch);
                                    ImGui::TableSetupColumn("legB", ImGuiTableColumnFlags_WidthStretch);

                                    ImGui::PushFont(nullptr, legFont);
                                    const float legRowH = ImGui::GetTextLineHeight() + 8.0f * dpiScale;
                                    const float valW = ImGui::CalcTextSize("Reboot").x + 6.0f;
                                    ImGui::PopFont();

                                    const int legSplit = (optTotal + 1) / 2;   // 5 then 4
                                    const float legH = legSplit * legRowH;
                                    const float dialH = dialR * 2.0f;

                                    auto legendCol = [&](const char* tid, int first, int last) {
                                        // Centre the shorter block against the taller,
                                        // whichever that turns out to be - the previous
                                        // version only handled the dial being taller,
                                        // so the card tilted the moment it was not.
                                        if (dialH > legH) ImGui::Dummy(ImVec2(0, (dialH - legH) * 0.5f));

                                        ImGui::PushFont(nullptr, legFont);
                                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                            ImVec2(ImGui::GetStyle().ItemSpacing.x, 8.0f * dpiScale));
                                        // 🔴 A REAL COLUMN FOR THE VALUE, not SameLine
                                        // to an absolute x. The previous version placed
                                        // the value at (columnWidth - textWidth), which
                                        // is correct only while the label ends before
                                        // that point - so "Global timer" ran straight
                                        // underneath its own "Reboot". A fixed column
                                        // sized to the widest state word cannot overlap;
                                        // worst case a long label clips, which is
                                        // legible where overlapping text is not.
                                        if (ImGui::BeginTable(tid, 2,
                                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
                                            ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthStretch);
                                            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthFixed, valW);
                                            for (int i = first; i < last; ++i) {
                                                const OptRow& r = optRows[i];
                                                ImGui::TableNextRow();
                                                ImGui::TableSetColumnIndex(0);
                                                ImGui::TextDisabled("%s", r.name);
                                                ImGui::TableSetColumnIndex(1);
                                                const char* val = stateText(r);
                                                const float tw = ImGui::CalcTextSize(val).x;
                                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, valW - tw));
                                                ImGui::TextColored(
                                                    ImGui::ColorConvertU32ToFloat4(
                                                        r.on ? stateU32(r) : IM_COL32(92, 84, 115, 255)),
                                                    "%s", val);
                                            }
                                            ImGui::EndTable();
                                        }
                                        ImGui::PopStyleVar();
                                        ImGui::PopFont();
                                        };

                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    if (legH > dialH) ImGui::Dummy(ImVec2(0, (legH - dialH) * 0.5f));
                                    {
                                        // The arcs ARE the legend - same nine items,
                                        // same order, same state colours.
                                        OptiMaxUi::RingSegment segs[IM_ARRAYSIZE(optRows)];
                                        for (int i = 0; i < optTotal; ++i) {
                                            segs[i].label = optRows[i].name;
                                            segs[i].state = stateText(optRows[i]);
                                            segs[i].col = stateU32(optRows[i]);
                                        }
                                        SegmentedRing("optring", segs, optTotal, ringVal, ringUnit,
                                            dialR, m_fontNumber);
                                    }

                                    ImGui::TableSetColumnIndex(1);
                                    legendCol("legcolA", 0, legSplit);

                                    ImGui::TableSetColumnIndex(2);
                                    legendCol("legcolB", legSplit, optTotal);

                                    ImGui::EndTable();
                                }
                            }
                            ImGui::EndChild();
                            ImGui::EndTable();
                        }
                        ImGui::Dummy(ImVec2(0, 6 * dpiScale));

                        // ---- CONTROLS: one row per toggle, not one card ----
                        // The mockup's biggest structural win. Each row carries the
                        // state chip and the switch, and the paragraph that used to
                        // sit in a card body moves into the hover tooltip - so the
                        // list is shorter AND says more, including the caveats that
                        // otherwise only exist in release notes nobody reads.
                        Eyebrow("CONTROLS  -  hover a row for what it actually does");
                        // Rows carry their own 2px gap, so ItemSpacing.y is zeroed
                        // inside this card - otherwise ImGui adds another 12px after
                        // each one and the fourth row falls off the bottom, which is
                        // exactly what was happening to GPU interrupts.
                        // TWO COLUMNS. At full width each row left a long gap between
                        // its description and its switch - the switch has to sit hard
                        // right, so a wide row is mostly empty. Halving the width
                        // closes that gap and fits seven rows in four rows' height,
                        // which is what stops the page needing a scroll to reach the
                        // footer.
                        const int ctrlRowCount = 7;
                        const int ctrlGridRows = (ctrlRowCount + 1) / 2;
                        // Height asked of the widget rather than assumed. This used
                        // to be a hardcoded 56 against the widget's own hardcoded
                        // 54 - two constants that had to agree and no way to make
                        // them, which is how the fourth row went missing once.
                        ImGui::BeginChild("CtrlRows",
                            ImVec2(0, ctrlGridRows * ToggleRowPitch() + ImGui::GetStyle().WindowPadding.y * 2.0f),
                            true, staticCardFlags);
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
                        if (ImGui::BeginTable("CtrlGrid", 2,
                            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
                            bool v;
                            // Fills left-to-right, top-to-bottom. Called before each
                            // row so the row itself stays a plain ToggleRow call and
                            // the layout lives in one place.
                            int ctrlCell = 0;
                            auto nextCtrl = [&]() {
                                if (ctrlCell % 2 == 0) ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(ctrlCell % 2);
                                ctrlCell++;
                                };
                            nextCtrl();

                            v = g_isCoreActive.load();
                            if (ToggleRow(OMX_ICON_BOLT, "Core tweaks",
                                "Timer, GPU priority, network latency",
                                v ? "ON" : "OFF", v ? ChipKind::On : ChipKind::Off, &v,
                                "Raises the system timer to 0.5ms, sets GPU scheduling priority to high, "
                                "and lifts Windows' network throttling. Turning it off restores all three "
                                "to the Windows defaults.")) {
                                if (v) ApplyCoreTweaksNow(localAntiCheatActive);
                                else   RevertCoreTweaksNow();
                            }

                            nextCtrl();

                            v = g_autoBoostEnabled.load();
                            {
                                char sub[128];
                                if (g_isGameRunning.load()) sprintf_s(sub, "Boosting the running game");
                                else sprintf_s(sub, "Watching for a game from your library");
                                if (ToggleRow(OMX_ICON_PLAY, "Auto-boost", sub,
                                    v ? "ON" : "OFF", v ? ChipKind::On : ChipKind::Off, &v,
                                    "Watches for a game from your library, applies the tweaks when one "
                                    "starts and puts them back when it closes. Per-game process tweaks are "
                                    "skipped while an anti-cheat is running.")) {
                                    g_autoBoostEnabled = v; SavePersistentState();
                                }
                            }

                            // Both directions, as of the Tweaks-page pass. This row
                            // used to offer only OFF, because the confirmation
                            // dialog was declared inside the throttling card and
                            // reproducing it here would have meant two copies of
                            // the same warning. The dialog now lives at page level
                            // and is raised by a flag, so both routes open the one
                            // popup and this row can be wired properly.
                            nextCtrl();
                            v = g_isolationActive.load();
                            if (ToggleRow(OMX_ICON_MEM, "Background throttling",
                                v ? "Background apps lowered - your game excluded"
                                  : "Background apps run at their normal priority",
                                v ? "ON" : "OFF", v ? ChipKind::On : ChipKind::Off, &v,
                                "Turning it off restores only what OptiMax lowered - it cannot undo "
                                "throttling from an earlier session, so restart anything that still feels "
                                "slow. Your game is always excluded.")) {
                                if (v) {
                                    // Same route the deleted card took: the state flag
                                    // is set here, the warning is shown, and the actual
                                    // throttling happens when it is accepted.
                                    g_isolationActive = true;
                                    wantThrottleConfirm = true;
                                }
                                else {
                                    g_isolationActive = false;
                                    SavePersistentState();
                                    SafeAddLog("SYSTEM: Background app throttling turned off.");
                                    RevertIsolationPriorities();
                                }
                            }

                            // REBOOT rather than ON, because the setting is saved and
                            // not yet in effect. A two-position switch cannot say that,
                            // which is exactly why the chip exists.
                            nextCtrl();
                            v = g_irqPinned.load();
                            if (ToggleRow(OMX_ICON_DIAMOND, "GPU interrupts",
                                "Pinned to a performance core",
                                v ? "REBOOT" : "OFF", v ? ChipKind::Pending : ChipKind::Off, &v,
                                "Written and waiting. Windows only re-reads interrupt affinity at boot, so "
                                "this does nothing until you restart. The switch is on because the setting "
                                "is saved, not because it is in effect.")) {
                                if (v) OptiMax::V3::PinGpuIrq(); else OptiMax::V3::RestoreGpuIrq();
                                g_irqPinned = v; SavePersistentState();
                            }

                            // ---- rows inherited from the cards that were removed ----
                            nextCtrl();
                            v = g_coresUnparked.load();
                            if (ToggleRow(OMX_ICON_CPU, "Keep cores awake",
                                "Core parking off, performance preference maxed",
                                v ? "ON" : "OFF", v ? ChipKind::On : ChipKind::Off, &v,
                                "Stops Windows parking idle cores and pins the energy performance "
                                "preference to maximum, so a core is never asleep when a frame needs it. "
                                "Costs idle power, and it is what Core tweaks leaves alone when this is on.")) {
                                // Same predicate as the Tweaks page row: all three
                                // of these report their real outcome, so the switch
                                // waits for them rather than moving regardless.
                                bool parkOk;
                                if (v) {
                                    const bool coresOk = OptiMax::AdvancedPower::DisableCoreParking();
                                    const bool eppOk = OptiMax::AdvancedPower::SetEPPToPerformance();
                                    if (coresOk && eppOk) SafeAddLog("POWER: Core parking disabled - all cores stay awake.");
                                    parkOk = coresOk && eppOk;
                                }
                                else {
                                    parkOk = OptiMax::AdvancedPower::RestoreProcessorPowerSettings();
                                }
                                if (parkOk) { g_coresUnparked = v; SavePersistentState(); }
                            }

                            // Disabled rather than hidden on limited hardware, so the
                            // reason is visible instead of the row just being absent.
                            nextCtrl();
                            v = g_settingAutoRam.load();
                            {
                                char sub[96];
                                if (g_IsLowSpec) sprintf_s(sub, "Unavailable - limited hardware detected");
                                else sprintf_s(sub, "Trims background apps above %d%% memory",
                                    g_ramCleanThreshold.load());
                                if (ToggleRow(OMX_ICON_MEM, "Automatic RAM cleaning", sub,
                                    g_IsLowSpec ? "N/A" : (v ? "ON" : "OFF"),
                                    g_IsLowSpec ? ChipKind::Unavailable : (v ? ChipKind::On : ChipKind::Off),
                                    &v,
                                    "Trims background programs' memory when usage passes your threshold, "
                                    "which is set in Settings. Your game is never touched, and cleaning is "
                                    "deferred while the system is already struggling.",
                                    !g_IsLowSpec)) {
                                    g_settingAutoRam = v; SavePersistentState();
                                }
                            }

                            // Reads live rather than from a stored flag: Explorer can
                            // be closed or restarted by anything, so a remembered
                            // value would go stale the moment it was.
                            nextCtrl();
                            {
                                const bool expRunning = OptiMax::IsExplorerRunning();
                                bool shellClosed = !expRunning;
                                if (ToggleRow(OMX_ICON_GRID, "Explorer shell",
                                    expRunning ? "Desktop and taskbar are running"
                                               : "Desktop and taskbar are closed",
                                    expRunning ? "OFF" : "ON",
                                    expRunning ? ChipKind::Off : ChipKind::On, &shellClosed,
                                    "Closes the desktop and taskbar to free their memory. Expect no frame "
                                    "rate change - Explorer sits idle while you play, and compositing is "
                                    "done by a separate process.")) {
                                    OptiMax::ToggleExplorer();
                                    SafeAddLog(expRunning ? "SYSTEM: Explorer shell closed."
                                                          : "SYSTEM: Explorer shell restarted.");
                                }
                            }
                            ImGui::EndTable();
                        }
                        ImGui::PopStyleVar();   // ItemSpacing pushed before the rows
                        ImGui::EndChild();
                        ImGui::Dummy(ImVec2(0, 6 * dpiScale));
                    }

                    if (ImGui::BeginTable("DashboardGrid", 1, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        // Sized to its four rows rather than borrowed from the
                        // shared 235px cardHeight, which is tuned for the tweak
                        // cards' title + paragraph + toggle + button stack. This
                        // card is a title and four one-line rows, so the shared
                        // height left it with a third of its area empty.
                        // cardHeight itself is deliberately untouched - the Games
                        // and Settings cards still depend on it.
                        const float specRowH = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
                        // Measured under the BOLD face, which is 28px against the
                        // 23px body - taking the body height here would under-size
                        // the card by the difference and clip the last row.
                        float specTitleH = ImGui::GetTextLineHeight();
                        if (m_fontBold) {
                            ImGui::PushFont(m_fontBold);
                            specTitleH = ImGui::GetTextLineHeight();
                            ImGui::PopFont();
                        }
                        const float specsCardH =
                            ImGui::GetStyle().WindowPadding.y * 2.0f
                            + specTitleH                      // the bold title
                            + 4.0f * dpiScale                 // the spacer below it
                            + specRowH * 5.0f;                // OS / CPU / GPU / RAM, +1 row of
                                                              // slack so a long GPU name that
                                                              // wraps cannot clip the RAM row
                        ImGui::BeginChild("Specs_Card", ImVec2(0, specsCardH), true, staticCardFlags);
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


                        ImGui::EndTable();
                    }

                    ImGui::Spacing();
                    ImGui::BeginChild("C4", ImVec2(0, 75.0f * dpiScale), true, staticCardFlags);
                    ImGui::SetCursorPosY((75.0f * dpiScale - globalBtnHeight) * 0.5f);
                    // Edit Game Library was the one control worth keeping from the
                    // deleted Automatic Game Detection card. Paired with Turn Off All
                    // Tweaks here rather than left floating, and it still just jumps
                    // to the My Games page.
                    size_t libCount = 0;
                    { std::lock_guard<std::mutex> lock(g_configMutex); libCount = g_targetGames.size(); }
                    char libBtnText[64];
                    sprintf_s(libBtnText, "Edit game library (%zu)", libCount);
                    const float halfBtn =
                        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                    if (PremiumButton(libBtnText, ImVec2(halfBtn, globalBtnHeight))) { activeTab = TAB_GAMES; gamesForceLibrary = true; }
                    ImGui::SameLine();
                    // 🔴 RENAMED FROM "Turn Off All Tweaks", WHICH WAS NOT TRUE.
                    //
                    // It reverts twelve tweaks and reaches none of these nine:
                    // privacy/tracking, extra background services, telemetry
                    // scheduled tasks, menu delays, 1:1 mouse input, GameDVR,
                    // MMCSS scheduling, the global timer, and MPO. The name was
                    // accurate when it was written and silently stopped being so
                    // as the Debloat and Experimental pages grew - nothing tied
                    // the label to the list.
                    //
                    // The word "all" is the whole problem: a user who wants
                    // everything undone presses this, is told it is done, and
                    // nine changes remain on their machine. That is the defect
                    // class this release exists to remove, in a button caption.
                    //
                    // The complete path is Settings > Restore Windows Defaults,
                    // which clears all twenty-one flags and calls
                    // RestoreAllToWindowsDefaults. Verified: this button's twelve
                    // are a strict SUBSET of those twenty-one, so nothing is
                    // cleared here that the full restore would leave applied.
                    //
                    // ⚠️ THERE IS NO PRINCIPLE BEHIND WHICH TWELVE. It is the set
                    // that existed when the button was written, and the boundary
                    // is historical rather than meaningful - do not describe it as
                    // "the runtime ones" or "the ones re-applied at launch". The
                    // launch re-apply block covers only EIGHT of them
                    // (OptiMax_Profiler.cpp:1467-1508); auto-boost, boot timers,
                    // the P-core lock and exclusive fullscreen are not in it. An
                    // earlier draft of the tooltip claimed exactly that and was
                    // wrong, which is the same overstatement this rename exists to
                    // remove. Say what it reverts; do not invent why.
                    if (PremiumButton("Turn Off Main Tweaks", ImVec2(halfBtn, globalBtnHeight))) {
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
                        // reverts everything it claims to - and, since the rename, it
                        // no longer claims more than it reverts either.
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

                        // "Turn Off Main Tweaks" must drop the in-use hold too, or
                        // Windows keeps deferring maintenance for a session the
                        // user has explicitly ended.
                        OptiMax::GameSession::End();
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

                        // The log gets the same correction as the caption. It said
                        // "All tweaks turned off" while nine were untouched, so
                        // anyone reading the log to check what happened was told
                        // the same untrue thing as the person who pressed it.
                        SafeAddLog(needsRestart
                            ? "SYSTEM: Main tweaks turned off. Privacy, extra services, telemetry tasks, menu delays, mouse input, GameDVR, MMCSS, the global timer and MPO are not affected - use Settings > Restore Windows Defaults for those. Restart your PC so the VBS, MSI mode, GPU interrupt and boot timer changes take effect."
                            : "SYSTEM: Main tweaks turned off. Privacy, extra services, telemetry tasks, menu delays, mouse input, GameDVR, MMCSS, the global timer and MPO are not affected - use Settings > Restore Windows Defaults for those.");
                    }
                    // The boundary named BEFORE the click, not only in the log
                    // afterwards. Someone who wants everything undone needs to
                    // know this is not it while they still have the choice.
                    if (ImGui::IsItemHovered()) {
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13, 11));
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(360.0f);
                        ImGui::TextUnformatted("Turn Off Main Tweaks");
                        ImGui::Separator();
                        ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::ColorConvertU32ToFloat4(OptiMaxUi::kDim));
                        ImGui::TextUnformatted(
                            "Reverts twelve tweaks: core tweaks, auto-boost, core parking, boot "
                            "timers, background throttling, the P-core lock, services, MSI mode, "
                            "TCP latency, exclusive fullscreen, VBS and GPU interrupts.\n\n"
                            "It does NOT touch privacy, extra services, telemetry tasks, menu "
                            "delays, 1:1 mouse input, GameDVR, MMCSS, the global timer or MPO. "
                            "For those, use Settings > Restore Windows Defaults, which clears "
                            "everything.");
                        ImGui::PopStyleColor();
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                        ImGui::PopStyleVar();
                    }
                    ImGui::EndChild();

                    if (!isAdmin) { ImGui::EndDisabled(); }
                }

                else if (activeTab == TAB_GAMES && gamesSubTab == 0) {
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

                else if (activeTab == TAB_GAMES && gamesSubTab == 1) {
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

                        // THE FEATURE: grouped by the section headings in profiles.db
                        // ("# --- Competitive FPS ---"), one collapsible group each, so a
                        // 30+ entry list is browsable instead of being one long table.
                        // The category rides in on a comment rather than a seventh field -
                        // see ParseDatabase for why a seventh field would have broken every
                        // client already in the wild.
                        //
                        // std::map orders the groups alphabetically, which is stable and
                        // searchable; entries with no section fall into "Other", forced
                        // last so an uncategorised straggler never sits above real groups.
                        std::map<std::string, std::vector<const OptiMax::CloudSync::GameProfile*>> grouped;
                        for (const auto& entry : cloudProfiles) {
                            const std::string& c = entry.second.category;
                            grouped[c.empty() ? "\xFF" "Other" : c].push_back(&entry.second);
                        }

                        auto DrawGroupTable = [&](const std::string& id,
                            const std::vector<const OptiMax::CloudSync::GameProfile*>& rows) {
                                if (!ImGui::BeginTable(id.c_str(), 7, cloudTableFlags)) return;
                                ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthStretch, 0.24f);
                                ImGui::TableSetupColumn("Engine", ImGuiTableColumnFlags_WidthStretch, 0.14f);
                                ImGui::TableSetupColumn("1 Thread/Core", ImGuiTableColumnFlags_WidthStretch, 0.11f);
                                ImGui::TableSetupColumn("High Priority", ImGuiTableColumnFlags_WidthStretch, 0.10f);
                                ImGui::TableSetupColumn("Frequent RAM Clean", ImGuiTableColumnFlags_WidthStretch, 0.12f);
                                ImGui::TableSetupColumn("No Fullscreen Opt.", ImGuiTableColumnFlags_WidthStretch, 0.11f);
                                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.18f);
                                ImGui::TableHeadersRow();

                                for (const OptiMax::CloudSync::GameProfile* pp : rows) {
                                    const OptiMax::CloudSync::GameProfile& p = *pp;

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
                            };

                        int groupIndex = 0;
                        for (const auto& g : grouped) {
                            // "\xFF" is the sort key that pins Other last; never show it.
                            std::string label = (g.first.rfind("\xFF", 0) == 0) ? g.first.substr(1) : g.first;
                            char header[160];
                            sprintf_s(header, "%s  (%d)##cloudgrp%d", label.c_str(), (int)g.second.size(), groupIndex);

                            if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
                                char tableId[64];
                                sprintf_s(tableId, "CloudProfileTable%d", groupIndex);
                                DrawGroupTable(tableId, g.second);
                                ImGui::Spacing();
                            }
                            groupIndex++;
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

                else if (activeTab == TAB_TWEAKS) {
                    // ==========================================================
                    // THE TWEAKS PAGE - four pages merged into one.
                    //
                    // Power & Timers, Services & Cleanup, Experimental and
                    // Debloat were named after the part of Windows they touch,
                    // so finding a tweak meant knowing how it was implemented.
                    // "Is MSI mode under Power & Timers or Experimental?" is
                    // unguessable. These four sections are named after what the
                    // user is trying to achieve instead.
                    //
                    // 🔴 "Experimental" deliberately stops being a place. Its
                    // contents were related only by RISK, which is a property
                    // and not a category - so VBS and MSI mode hid on a page
                    // nobody opened. They now sit in the section they
                    // functionally belong to, carry an ADVANCED chip, and stay
                    // behind the one "show advanced" switch. Same gate, findable.
                    //
                    // NONE OF THE TWEAK LOGIC CHANGED in this merge. Every
                    // handler below calls the same function the old card called,
                    // in the same order, with the same success predicate - so
                    // anything that breaks here is a UI bug, not a broken tweak.
                    // ==========================================================
                    using namespace OptiMaxUi;

                    // Session-scoped, deliberately not persisted: a filter left
                    // on from last week would hide rows on launch and read as
                    // tweaks having disappeared.
                    static char tweakSearch[96] = "";
                    static int  tweakFilter = 0;      // 0 all, 1 on, 2 needs reboot, 3 unavailable
                    static bool showAdvanced = false;
                    static bool secOpen[(int)TweakSection::COUNT] = { true, true, true, true };

                    std::vector<TweakDesc> tweaks;
                    tweaks.reserve(24);

                    // Handlers capture by reference and are invoked in this same
                    // frame, in this same scope - the registry never outlives the
                    // function that built it. That is what lets them reach
                    // app-level state (the anti-cheat flag, the game library, the
                    // throttling confirmation) without a lower header having to
                    // know those exist.

                    // ---------- PERFORMANCE ----------
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_BOLT;
                        t.title = "Core tweaks";
                        t.subtitle = "Timer, GPU priority, network latency";
                        t.tooltip = "Raises the system timer to 0.5ms, sets GPU scheduling priority to high, "
                                    "and lifts Windows' network throttling. Turning it off restores all three "
                                    "to the Windows defaults.";
                        t.state = g_isCoreActive.load();
                        t.apply = [&](bool on) {
                            if (on) ApplyCoreTweaksNow(localAntiCheatActive);
                            else    RevertCoreTweaksNow();
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_PLAY;
                        t.title = "Auto-boost";
                        t.subtitle = g_isGameRunning.load() ? "Boosting the running game"
                                                            : "Watching for a game from your library";
                        t.tooltip = "Watches for a game from your library, applies the tweaks when one starts "
                                    "and puts them back when it closes. Per-game process tweaks are skipped "
                                    "while an anti-cheat is running.";
                        t.state = g_autoBoostEnabled.load();
                        t.apply = [&](bool on) { g_autoBoostEnabled = on; SavePersistentState(); };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_CPU;
                        t.title = "Keep cores awake";
                        t.subtitle = "Core parking off, performance preference maxed";
                        t.tooltip = "Stops Windows parking idle cores and pins the energy performance "
                                    "preference to maximum, so a core is never asleep when a frame needs it. "
                                    "Costs a little idle power.";
                        t.state = g_coresUnparked.load();
                        t.apply = [&](bool on) {
                            // Both directions report their own outcome, so the
                            // switch only moves when the writes actually landed.
                            bool ok;
                            if (on) {
                                const bool coresOk = OptiMax::AdvancedPower::DisableCoreParking();
                                const bool eppOk = OptiMax::AdvancedPower::SetEPPToPerformance();
                                if (coresOk && eppOk) SafeAddLog("POWER: Core parking disabled - all cores stay awake.");
                                ok = coresOk && eppOk;
                            }
                            else {
                                ok = OptiMax::AdvancedPower::RestoreProcessorPowerSettings();
                            }
                            if (ok) { g_coresUnparked = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_TIMER;
                        t.title = "Boot timer settings";
                        t.subtitle = "HPET and dynamic tick off in the boot configuration";
                        t.pending = true;
                        t.tooltip = "Turns off the HPET timer and dynamic tick in the boot configuration, so "
                                    "Windows uses the CPU's own clock instead. Windows only reads this at "
                                    "boot, so the switch being on means the setting is saved - not that it "
                                    "is in effect yet.";
                        t.state = g_bootOptimized.load();
                        t.apply = [&](bool on) {
                            bool ok;
                            if (on) { RestorePointBeforeTweak(); ok = OptiMax::Bootloader::OptimizeTimers(); }
                            else { ok = OptiMax::Bootloader::RestoreBootTimers(); }
                            // This one matters more than most: the user is being
                            // asked to reboot on the strength of it.
                            if (ok) { g_bootOptimized = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_SPARK;
                        t.title = "Boost game scheduling";
                        t.subtitle = "Raises the MMCSS 'Games' task priority";
                        t.tooltip = "Raises the scheduling profile Windows gives games' render threads via the "
                                    "MMCSS 'Games' task (Priority and SFIO Priority). Modest but real, and "
                                    "safe. No restart needed.";
                        t.state = g_mmcssGames.load();
                        t.apply = [&](bool on) {
                            if (OptiMax::V3::SetMMCSSGamesProfile(on)) { g_mmcssGames = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_CLOCK;
                        t.title = "Global timer resolution";
                        t.subtitle = "Timer requests apply system-wide, not just to the foreground app";
                        t.pending = true;
                        t.tooltip = "Makes the low-latency timer resolution OptiMax requests apply system-wide "
                                    "instead of only to the foreground app (Windows 11 22H2+). Complements the "
                                    "timer tweaks. Needs a restart.";
                        t.state = g_globalTimerRes.load();
                        t.apply = [&](bool on) {
                            if (OptiMax::V3::SetGlobalTimerResolution(on)) { g_globalTimerRes = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_DIAMOND;
                        t.title = "TCP latency tweaks";
                        t.subtitle = "Send small packets immediately instead of batching";
                        t.tooltip = "Disables Nagle's algorithm and delayed ACKs on your active network "
                                    "adapters. Helps online latency, slightly worse for bulk transfers.";
                        t.state = g_networkTweaked.load();
                        t.apply = [&](bool on) {
                            // THE FIX carried forward from the old card, and
                            // tightened: it used to persist the flag BEFORE
                            // writing, then verify, then log the truth - so a
                            // refused write left the switch on beside a log line
                            // saying it had failed. The verifier is authoritative
                            // here, so the flag now follows it.
                            if (on) {
                                OptiMax::V3::BypassNagleAlgorithm();
                                if (OptiMaxVerifier::CheckNetworkTweaks()) {
                                    SafeAddLog("TWEAK: TCP latency tweaks applied to active network adapters and confirmed in the registry.");
                                    g_networkTweaked = true; SavePersistentState();
                                }
                                else SafeAddLog("TWEAK FAILED: no adapter shows the TCP latency values - the writes did not land, or no Ethernet/Wi-Fi adapter is currently connected.");
                            }
                            else {
                                OptiMax::V3::RestoreNagleAlgorithm();
                                if (!OptiMaxVerifier::CheckNetworkTweaks()) {
                                    SafeAddLog("TWEAK: TCP latency tweaks removed and confirmed gone. Restart to return to default TCP timing.");
                                    g_networkTweaked = false; SavePersistentState();
                                }
                                else SafeAddLog("TWEAK FAILED: TCP latency overrides are still present on at least one adapter after the restore attempt.");
                            }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_FLASK;
                        t.title = "GPU MSI mode";
                        t.subtitle = "Message-signalled interrupts for the GPU";
                        t.advanced = true; t.pending = true;
                        t.tooltip = "Switches the GPU to message-signalled interrupts, if not already enabled. "
                                    "Most modern GPUs default to this, so it often changes nothing. Needs a "
                                    "restart.";
                        t.state = g_msiEnabled.load();
                        t.apply = [&](bool on) {
                            // Same tightening as the TCP row: InjectMSIMode and
                            // RestoreMSIMode ignore every RegSetValueExW result,
                            // so the registry read-back is the only honest signal
                            // - and the flag now follows it rather than the intent.
                            if (on) {
                                RestorePointBeforeTweak();
                                OptiMax::V3::InjectMSIMode();
                                if (OptiMaxVerifier::CheckMSIMode()) {
                                    SafeAddLog("TWEAK: GPU MSI mode enabled and confirmed in the registry. Restart to take effect.");
                                    g_msiEnabled = true; SavePersistentState();
                                }
                                else SafeAddLog("TWEAK FAILED: GPU MSI mode was not written - the registry still shows it off. Administrator rights are required.");
                            }
                            else {
                                OptiMax::V3::RestoreMSIMode();
                                if (!OptiMaxVerifier::CheckMSIMode()) {
                                    SafeAddLog("TWEAK: GPU MSI mode setting removed and confirmed gone. Restart to return to the driver default.");
                                    g_msiEnabled = false; SavePersistentState();
                                }
                                else SafeAddLog("TWEAK FAILED: the GPU MSI mode override is still present after the restore attempt. Administrator rights are required.");
                            }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_DIAMOND;
                        t.title = "GPU interrupt affinity";
                        t.subtitle = "Pinned to a performance core";
                        t.advanced = true; t.pending = true;
                        t.tooltip = "Pins GPU driver interrupts to one performance core so they don't collide "
                                    "with input and audio. The core is picked from real CPU topology. Windows "
                                    "only re-reads interrupt affinity at boot, so the switch is on because the "
                                    "setting is saved, not because it is in effect.";
                        t.state = g_irqPinned.load();
                        t.apply = [&](bool on) {
                            if (on) { RestorePointBeforeTweak(); OptiMax::V3::PinGpuIrq(); }
                            else { OptiMax::V3::RestoreGpuIrq(); }
                            g_irqPinned = on; SavePersistentState();
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_FLASK;
                        t.title = "Lock games to P-cores";
                        t.advanced = true;
                        t.available = isHeterogeneousCPU;
                        t.subtitle = isHeterogeneousCPU
                            ? "Detected games are restricted to the performance cores"
                            : "No efficiency cores on this CPU - nothing to do";
                        t.tooltip = isHeterogeneousCPU
                            ? "Restricts detected games to the CPU's performance cores, applied automatically "
                              "when a game starts. Not every game benefits, so you can override it per game "
                              "in Games."
                            : "This CPU has a single core type, so there is no efficiency core to keep a game "
                              "off. Availability is read from the real CPU topology, not from the model name.";
                        t.state = g_pCoresLocked.load();
                        t.apply = [&](bool on) {
                            g_pCoresLocked = on; SavePersistentState();
                            SafeAddLog(on ? "TWEAK: P-core lock on - applied automatically when a game starts."
                                          : "TWEAK: P-core lock off.");
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Performance;
                        t.icon = OMX_ICON_WARN;
                        t.title = "Disable VBS";
                        t.subtitle = "Costs you Memory Integrity and Credential Guard";
                        t.advanced = true; t.pending = true;
                        t.tooltip = "A real gain of a few percent - Windows stops running under a hypervisor. "
                                    "It also removes genuine security features: Memory Integrity and "
                                    "Credential Guard both go off. Needs a restart.";
                        t.state = g_vbsDisabled.load();
                        t.apply = [&](bool on) {
                            if (on) {
                                RestorePointBeforeTweak();
                                if (OptiMax::V3::DisableVBS()) {
                                    g_vbsDisabled = true; SavePersistentState();
                                    SafeAddLog("TWEAK: VBS and Memory Integrity disabled - Credential Guard is now off too. Restart to take effect.");
                                }
                                else SafeAddLog("TWEAK FAILED: VBS could not be disabled - administrator rights are required. Nothing was changed.");
                            }
                            else {
                                // RestoreVBS reads the keys back and logs the real
                                // state itself, so a failure here can never read as
                                // "you're protected" - which is what the dead revert
                                // path this replaced used to say.
                                if (OptiMax::V3::RestoreVBS()) { g_vbsDisabled = false; SavePersistentState(); }
                            }
                            };
                        tweaks.push_back(std::move(t));
                    }

                    // ---------- INTERFERENCE ----------
                    {
                        TweakDesc t;
                        t.section = TweakSection::Interference;
                        t.icon = OMX_ICON_MEM;
                        t.title = "Background throttling";
                        t.subtitle = g_isolationActive.load() ? "Background apps lowered - your game excluded"
                                                              : "Background apps run at their normal priority";
                        t.tooltip = "Drops background apps to Below Normal CPU, Very Low disk I/O and lowest "
                                    "memory page priority. Windows components, anti-cheat and your game are "
                                    "always excluded. Turning it off restores only what OptiMax lowered - it "
                                    "cannot undo throttling from an earlier session, so restart anything that "
                                    "still feels slow.";
                        t.state = g_isolationActive.load();
                        t.apply = [&](bool on) {
                            if (on) {
                                // The flag is set here, the warning is shown, and
                                // the throttling itself happens when it is accepted.
                                g_isolationActive = true;
                                wantThrottleConfirm = true;
                            }
                            else {
                                g_isolationActive = false;
                                SavePersistentState();
                                SafeAddLog("SYSTEM: Background app throttling turned off.");
                                RevertIsolationPriorities();
                            }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Interference;
                        t.icon = OMX_ICON_LIST;
                        t.title = "Telemetry & background services";
                        t.subtitle = "DiagTrack, SysMain, Windows Search, WAP push";
                        t.tooltip = "Disables telemetry collection and four background services. Search "
                                    "indexing and app preloading stop working until you restore them. A "
                                    "partial result is reported as a partial result, not as success.";
                        t.state = g_servicesSanitized.load();
                        t.apply = [&](bool on) {
                            if (on) {
                                RestorePointBeforeTweak();
                                const bool telemetryOk = OptiMax::Debloat::DisableTelemetry();
                                const bool servicesOk = OptiMax::Debloat::StopBloatServices();
                                // Set even on a partial failure: something may have
                                // changed, and clearing the flag would hide the only
                                // path that undoes it.
                                g_servicesSanitized = true;
                                if (telemetryOk && servicesOk) SafeAddLog("SERVICES: Telemetry and background services disabled.");
                                else SafeAddLog(std::string("SERVICES: PARTIAL - telemetry ") + (telemetryOk ? "disabled" : "NOT disabled")
                                    + ", background services " + (servicesOk ? "disabled" : "NOT fully disabled") + ". See the warnings above.");
                            }
                            else {
                                const bool telemetryOk = OptiMax::Debloat::RestoreTelemetry();
                                const bool servicesOk = OptiMax::Debloat::RestoreBloatServices();
                                // Only clear once the machine is actually back.
                                // Leaving it set keeps the off-switch reachable so a
                                // failed restore can be retried instead of looking
                                // like it worked.
                                g_servicesSanitized = !(telemetryOk && servicesOk);
                                if (telemetryOk && servicesOk) SafeAddLog("SERVICES: Services restored to their original startup types.");
                                else SafeAddLog(std::string("SERVICES: PARTIAL - telemetry ") + (telemetryOk ? "restored" : "NOT restored")
                                    + ", services " + (servicesOk ? "restored" : "NOT fully restored") + ". Retry with administrator rights.");
                            }
                            SavePersistentState();
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Interference;
                        t.icon = OMX_ICON_LIST;
                        t.title = "Extra background services";
                        t.subtitle = "Maps, Retail Demo, Fax, Parental Controls, WMP sharing, Geolocation";
                        t.tooltip = "Disables lower-value services on top of the telemetry set above. "
                                    "Geolocation is included, so anything that asks Windows where you are "
                                    "stops working until you restore them.";
                        t.state = g_extendedServices.load();
                        t.apply = [&](bool on) {
                            if (OptiMax::Debloat::SetExtendedServices(on)) { g_extendedServices = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Interference;
                        t.icon = OMX_ICON_LIST;
                        t.title = "Telemetry scheduled tasks";
                        t.subtitle = "Customer Experience, Compatibility Appraiser, disk diagnostics";
                        t.tooltip = "Disables the data-collection scheduled tasks through the Task Scheduler "
                                    "API - no schtasks.exe is launched. These are the tasks that wake up and "
                                    "upload diagnostics while you are playing.";
                        t.state = g_telemetryTasks.load();
                        t.apply = [&](bool on) {
                            if (OptiMax::Debloat::SetTelemetryTasks(on)) { g_telemetryTasks = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::Interference;
                        t.icon = OMX_ICON_PLAY;
                        t.title = "GameDVR / Game Bar";
                        t.subtitle = "Stops the background recording pipeline";
                        t.tooltip = "Xbox Game Bar's capture pipeline runs even when you are not recording. "
                                    "A real, low-risk frame-time win. No restart needed.";
                        t.state = g_gameDvrDisabled.load();
                        t.apply = [&](bool on) {
                            if (OptiMax::V3::SetGameDVR(on)) { g_gameDvrDisabled = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }

                    // ---------- INPUT & DISPLAY ----------
                    {
                        TweakDesc t;
                        t.section = TweakSection::InputDisplay;
                        t.icon = OMX_ICON_SPARK;
                        t.title = "1:1 mouse input";
                        t.subtitle = "Turns off Enhance Pointer Precision";
                        t.tooltip = "The same physical movement always maps to the same in-game turn. The "
                                    "single most useful setting for consistent low-sensitivity aim. Applies "
                                    "instantly, no restart.";
                        t.state = g_mouseAccelDisabled.load();
                        t.apply = [&](bool on) {
                            if (OptiMax::V3::SetMouseAcceleration(on)) { g_mouseAccelDisabled = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::InputDisplay;
                        t.icon = OMX_ICON_GRID;
                        t.title = "Prefer exclusive fullscreen";
                        t.subtitle = "Fullscreen games bypass the compositor";
                        t.tooltip = "Lets fullscreen games take the display directly instead of going through "
                                    "the compositor. Alt-Tab gets slower. Applies to all games, not just one. "
                                    "Windows resets these keys on its own, so OptiMax re-applies them while "
                                    "this is on - which is why turning it off is what actually removes them.";
                        t.state = g_fseForced.load();
                        t.apply = [&](bool on) {
                            g_fseForced = on; SavePersistentState();
                            if (on) {
                                SafeAddLog("TWEAK: Exclusive fullscreen preferred. OptiMax will re-apply this if Windows resets it.");
                            }
                            else {
                                // g_fseForced is already false here, so the FSE
                                // watchdog will not undo this restore.
                                OptiMax::V3::RestoreFSE();
                            }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::InputDisplay;
                        t.icon = OMX_ICON_GRID;
                        t.title = "Menu & hover delays";
                        t.available = isWindows11;
                        t.subtitle = isWindows11 ? "Menus open instantly, tooltips appear at 10ms"
                                                 : "Windows 11 only - this PC runs Windows 10";
                        t.tooltip = isWindows11
                            ? "Drops the menu-open delay to 0ms and the mouse-hover delay to 10ms (Windows "
                              "ships both at 400ms). Applies live, no restart."
                            : "This tweak targets Windows 11's shell delays and is gated to it. Nothing is "
                              "written on this OS.";
                        t.state = g_snappyUI.load();
                        t.apply = [&](bool on) {
                            if (OptiMax::Debloat::SetSnappyUI(on)) { g_snappyUI = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        // Read live rather than from a stored flag: Explorer can be
                        // closed or restarted by anything, so a remembered value
                        // would go stale the moment it was.
                        const bool expRunning = OptiMax::IsExplorerRunning();
                        TweakDesc t;
                        t.section = TweakSection::InputDisplay;
                        t.icon = OMX_ICON_GRID;
                        t.title = "Close Explorer shell";
                        t.subtitle = expRunning ? "Desktop and taskbar are running"
                                                : "Desktop and taskbar are closed";
                        t.tooltip = "Closes the desktop and taskbar to free their memory. Expect no frame "
                                    "rate change - Explorer sits idle while you play, and compositing is done "
                                    "by a separate process.";
                        t.state = !expRunning;
                        t.apply = [expRunning](bool) {
                            OptiMax::ToggleExplorer();
                            SafeAddLog(expRunning ? "SYSTEM: Explorer shell closed."
                                                  : "SYSTEM: Explorer shell restarted.");
                            };
                        tweaks.push_back(std::move(t));
                    }

                    // ---------- PRIVACY & CLEANUP ----------
                    {
                        TweakDesc t;
                        t.section = TweakSection::PrivacyCleanup;
                        t.icon = OMX_ICON_CLOUD;
                        t.title = "Tracking & suggestions";
                        t.subtitle = "Advertising ID, activity history, tailored experiences, Copilot";
                        t.tooltip = "Turns off the advertising ID, activity history, tailored experiences, "
                                    "feedback prompts, Windows Copilot and Start-menu suggestions via "
                                    "registry policy.";
                        t.state = g_privacyTweaked.load();
                        t.apply = [&](bool on) {
                            // A partial apply reads as failure here. That is
                            // deliberate and it is safe: whatever DID land is still
                            // cleaned up by Settings > Restore Defaults, which
                            // clears every flag and calls
                            // RestoreAllToWindowsDefaults unconditionally - so an
                            // off switch is never a dead revert path.
                            //
                            // ⚠️ NOT the Dashboard's "Turn Off Main Tweaks", which
                            // reverts twelve and does not reach this one - which
                            // is why it is no longer called "All".
                            if (OptiMax::Debloat::SetPrivacyTweaks(on)) { g_privacyTweaked = on; SavePersistentState(); }
                            };
                        tweaks.push_back(std::move(t));
                    }
                    {
                        TweakDesc t;
                        t.section = TweakSection::PrivacyCleanup;
                        t.icon = OMX_ICON_MEM;
                        t.title = "Automatic RAM cleaning";
                        t.available = !g_IsLowSpec;
                        t.subtitle = g_IsLowSpec
                            ? "Unavailable - limited hardware detected"
                            : ("Trims background apps above " + std::to_string(g_ramCleanThreshold.load()) + "% memory");
                        t.tooltip = g_IsLowSpec
                            ? "A mechanical hard drive or under 16GB of RAM was found. On this hardware "
                              "trimming costs more in disk reads than it frees, so it is disabled."
                            : "Trims background programs' memory when usage passes your threshold, which is "
                              "set in Settings. Your game is never touched, and cleaning is deferred while "
                              "the system is already struggling.";
                        t.state = g_settingAutoRam.load();
                        t.apply = [&](bool on) { g_settingAutoRam = on; SavePersistentState(); };
                        tweaks.push_back(std::move(t));
                    }

                    // ==========================================================
                    // FILTER ONCE. THE CHIP COUNTS COME FROM THIS PASS.
                    //
                    // The four state-filter chips report how many rows each one
                    // WOULD select out of what search and the advanced gate have
                    // already left, so they are counted here, in the one pass that
                    // also decides what renders. They cannot drift from it.
                    //
                    // ⚠️ THE SECTION HEADERS ARE DELIBERATELY *NOT* COUNTED HERE.
                    // An earlier version did, and the header then read "4 OF 4 ON"
                    // whenever the On filter was active - true of the rows on
                    // screen, and useless. A section header answers "how much of
                    // this section is on", which is a fact about the section and
                    // must not move when someone types in the search box. It is
                    // counted per section below, from its own single pass.
                    // ==========================================================
                    auto lowerOf = [](std::string s) {
                        for (char& c : s) c = (char)tolower((unsigned char)c);
                        return s;
                        };
                    const std::string query = lowerOf(tweakSearch);

                    std::vector<int> visible;
                    visible.reserve(tweaks.size());
                    int cntAll = 0, cntOn = 0, cntReboot = 0, cntNa = 0;

                    for (int i = 0; i < (int)tweaks.size(); ++i) {
                        const TweakDesc& t = tweaks[i];
                        if (t.advanced && !showAdvanced) continue;
                        if (!query.empty()) {
                            const bool hit = lowerOf(t.title).find(query) != std::string::npos
                                || lowerOf(t.subtitle).find(query) != std::string::npos
                                || lowerOf(t.tooltip).find(query) != std::string::npos;
                            if (!hit) continue;
                        }

                        // Counted before the state filter, so the chips keep
                        // showing how many rows each one WOULD select. A chip
                        // reading "On 0" while On is the active filter is what
                        // tells the user the filter is the reason the page is
                        // empty.
                        ++cntAll;
                        const bool isOn = t.available && t.state;
                        if (isOn) ++cntOn;
                        if (isOn && t.pending) ++cntReboot;
                        if (!t.available) ++cntNa;

                        bool pass = true;
                        switch (tweakFilter) {
                        case 1: pass = isOn; break;
                        case 2: pass = isOn && t.pending; break;
                        case 3: pass = !t.available; break;
                        default: pass = true; break;
                        }
                        if (pass) visible.push_back(i);
                    }

                    // ---- search + filters ----
                    {
                        const float searchW = ImGui::GetContentRegionAvail().x * 0.42f;
                        ImGui::SetNextItemWidth(searchW);
                        ImGui::InputTextWithHint("##tweaksearch", "Search tweaks...",
                            tweakSearch, IM_ARRAYSIZE(tweakSearch));
                        ImGui::SameLine();
                        if (tweakSearch[0] && ImGui::Button("Clear")) tweakSearch[0] = '\0';

                        ImGui::Dummy(ImVec2(0, 4 * dpiScale));

                        if (FilterChip("All", cntAll, tweakFilter == 0)) tweakFilter = 0;
                        ImGui::SameLine();
                        if (FilterChip("On", cntOn, tweakFilter == 1)) tweakFilter = 1;
                        ImGui::SameLine();
                        if (FilterChip("Needs reboot", cntReboot, tweakFilter == 2)) tweakFilter = 2;
                        ImGui::SameLine();
                        if (FilterChip("Unavailable", cntNa, tweakFilter == 3)) tweakFilter = 3;

                        ImGui::SameLine();
                        ImGui::Dummy(ImVec2(12 * dpiScale, 0));
                        ImGui::SameLine();
                        ImGui::PushID("show_advanced");
                        // One gate for everything that used to live on the
                        // Experimental page. Same protection, but the tweaks are
                        // now findable in the section they belong to. The return
                        // is ignored deliberately: this changes what is shown, not
                        // what is applied, so there is nothing to run on a click.
                        PremiumToggle("Show advanced", &showAdvanced);
                        ImGui::PopID();
                    }

                    ImGui::Dummy(ImVec2(0, 8 * dpiScale));

                    // ---- sections ----
                    struct SectionMeta { const char* title; const char* why; };
                    static const SectionMeta kSections[(int)TweakSection::COUNT] = {
                        { "Performance",      "Raise throughput and lower latency for the running game" },
                        { "Interference",     "Stop Windows competing with your game for CPU and disk"  },
                        { "Input & display",  "Mouse feel, fullscreen behaviour and desktop response"   },
                        { "Privacy & cleanup","Turn tracking off, get junk out, keep memory free"       },
                    };

                    for (int s = 0; s < (int)TweakSection::COUNT; ++s) {
                        // 🔴 TWO DIFFERENT SETS, ON PURPOSE.
                        //
                        // `rows` is what RENDERS - search and the state filter
                        // applied. The header's "N OF M ON" describes the SECTION
                        // and deliberately ignores both, so M is the section's
                        // membership rather than however many rows survived the
                        // current view.
                        //
                        // The first version counted the visible rows, which made
                        // the header read "4 OF 4 ON" whenever the On filter was
                        // active - technically true of what was on screen, and
                        // useless: it answered a question nobody asks. What a
                        // reader wants from a section header is how much of THAT
                        // SECTION is on, which does not change when they type in
                        // the search box.
                        //
                        // ⚠️ Both numbers still come from ONE pass over ONE
                        // predicate, so they cannot disagree with EACH OTHER -
                        // that was the property worth keeping, and it survives.
                        // The advanced gate IS honoured, because counting rows the
                        // user has chosen not to see would be a third meaning
                        // again.
                        int onHere = 0, totalHere = 0;
                        for (const TweakDesc& t : tweaks) {
                            if ((int)t.section != s) continue;
                            if (t.advanced && !showAdvanced) continue;
                            ++totalHere;
                            if (t.available && t.state) ++onHere;
                        }

                        std::vector<int> rows;
                        for (int idx : visible) {
                            if ((int)tweaks[idx].section == s) rows.push_back(idx);
                        }
                        // A section with nothing left after filtering is hidden
                        // rather than shown empty - an empty card reads as a bug.
                        if (rows.empty()) continue;

                        ImGui::PushID(s);
                        // totalHere, NOT rows.size() - the header reports the
                        // section, not the filtered view. See the note above.
                        if (SectionHeader(kSections[s].title, kSections[s].why,
                            onHere, totalHere, &secOpen[s])) {

                            ImGui::BeginChild("rows",
                                ImVec2(0, rows.size() * ToggleRowPitch() + ImGui::GetStyle().WindowPadding.y * 2.0f),
                                true, staticCardFlags);
                            // Rows carry their own 2px gap, so ItemSpacing.y is
                            // zeroed - otherwise ImGui adds another 12px after each
                            // one and the last row falls off the bottom of the card.
                            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

                            for (int idx : rows) {
                                TweakDesc& t = tweaks[idx];
                                const char* chipText = nullptr;
                                const ChipKind kind = ChipKindFor(t, &chipText);
                                bool v = t.state;
                                ImGui::PushID(idx);
                                if (ToggleRow(t.icon, t.title.c_str(), t.subtitle.c_str(),
                                    chipText, kind, &v, t.tooltip.c_str(), t.available)) {
                                    // The handler owns the flag. Nothing is written
                                    // back to `t` - the registry is rebuilt next
                                    // frame from the globals, so a refused write
                                    // snaps the switch back on its own.
                                    if (t.apply) t.apply(v);
                                }
                                ImGui::PopID();
                            }

                            ImGui::PopStyleVar();
                            ImGui::EndChild();
                            ImGui::Dummy(ImVec2(0, 6 * dpiScale));
                        }
                        ImGui::PopID();
                    }

                    if (visible.empty()) {
                        ImGui::Dummy(ImVec2(0, 12 * dpiScale));
                        if (query.empty()) {
                            ImGui::TextDisabled("No tweaks match that filter.");
                        }
                        else {
                            ImGui::TextDisabled("Nothing matches \"%s\".", tweakSearch);
                        }
                        if (!showAdvanced) {
                            ImGui::TextDisabled("Advanced tweaks are hidden - turn on \"Show advanced\" to include them.");
                        }
                    }

                    // ==========================================================
                    // ACTIONS - the things here that are not toggles.
                    //
                    // Kept out of the sections above because they have no state
                    // to report: they run once and finish. Putting a one-shot
                    // button in a list of switches, with a chip that could only
                    // ever read OFF, would be the same lie the chips exist to
                    // stop telling.
                    //
                    // Hidden while a state filter is active, since none of them
                    // has a state to filter on.
                    // ==========================================================
                    {
                        auto actionMatches = [&](const char* name, const char* blurb) {
                            if (tweakFilter != 0) return false;
                            if (query.empty()) return true;
                            return lowerOf(name).find(query) != std::string::npos
                                || lowerOf(blurb).find(query) != std::string::npos;
                            };

                        const bool showClean = actionMatches("Clear caches and temp files",
                            "shader cache dns flush temporary files cleanup");
                        const bool showHealth = actionMatches("Windows Health Repair",
                            "dism sfc scannow restorehealth component store corruption repair");
                        const bool showStore = actionMatches("Remove Store apps",
                            "uwp bundled appx packages uninstall bloatware");

                        if (showClean || showHealth || showStore) {
                            ImGui::Dummy(ImVec2(0, 6 * dpiScale));
                            Eyebrow("ACTIONS  -  these run once, they are not settings");
                        }

                        if (showClean) {
                            // 🔴 NO FIXED-HEIGHT CARD. This was a 118px BeginChild with
                            // staticCardFlags, which disables scrolling - so once the
                            // bigger fonts landed, a 28px title plus two wrapped lines
                            // plus a button no longer fitted and the BUTTON WAS THE PART
                            // THAT VANISHED. Silently: a card with no scrollbar and no
                            // clipping indicator looks finished.
                            //
                            // Rendered inline instead, like the two actions below it -
                            // which is also why they never had the problem. A fixed
                            // height around wrapped text is a bug waiting for the next
                            // font change; the only safe versions either derive the
                            // height or do not fix one.
                            if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Clear caches & temp files"); if (m_fontBold) ImGui::PopFont();
                            ImGui::TextWrapped("Deletes GPU shader caches and temp files, and flushes the DNS cache. Games rebuild their shader cache next launch, so expect one slower session after.");
                            ImGui::Spacing();
                            // Blocked while a game is running: this deletes the GPU
                            // driver's shader cache and the running game is the
                            // process using it. Clearing it mid-session forces a
                            // live shader recompile - the exact stutter this app
                            // exists to remove.
                            {
                                const bool cleanBlocked = g_isGameRunning.load();
                                if (cleanBlocked) ImGui::BeginDisabled();
                                if (PremiumButton("Clear Caches & Temp Files", ImVec2(-1, globalBtnHeight))) { OptiMax::Purger::DeepClean(); }
                                if (cleanBlocked) {
                                    ImGui::EndDisabled();
                                    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Unavailable while a game is running - it would delete the shader cache in use.");
                                }
                            }
                            ImGui::Dummy(ImVec2(0, 6 * dpiScale)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 6 * dpiScale));
                        }

                        // --- Windows Health Repair (DISM + SFC) ---
                        if (showHealth) {
                            if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Windows Health Repair"); if (m_fontBold) ImGui::PopFont();
                            ImGui::TextWrapped("Runs Microsoft's own repair chain in order: DISM CheckHealth, ScanHealth, RestoreHealth, StartComponentCleanup, then SFC ScanNow. Repairs a damaged component store and replaces corrupted system files - worth doing if Windows Update fails, features break after an upgrade, or things behave oddly for no clear reason. RestoreHealth needs an internet connection.");
                            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "This can take 30+ minutes. It runs in the background - the app stays usable, and every stage is written to the log file.");
                            ImGui::Spacing();

                            const bool healthBusy = OptiMax::SystemHealth::g_running.load();
                            // Blocked while a game is running. DISM and SFC are not
                            // an anti-cheat concern - they never touch the game
                            // process - but ScanHealth and /scannow both saturate
                            // the disk for minutes at a time, which is exactly the
                            // stutter this app exists to remove.
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
                                // exception at a thread entry point calls
                                // std::terminate, and this one owns g_running - a
                                // throw would leave the button disabled for the rest
                                // of the session.
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
                            ImGui::Dummy(ImVec2(0, 6 * dpiScale)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 6 * dpiScale));
                        }

                        // --- Remove Store Apps (native PackageManager, full-width pick-list) ---
                        static std::vector<OptiMax::UWP::AppEntry> uwpApps;
                        static std::atomic<bool> uwpScanning{ false };
                        static std::atomic<bool> uwpRemoving{ false };
                        static std::atomic<bool> uwpScanned{ false };
                        static std::mutex uwpMutex;

                        if (showStore) {
                            if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "Remove Store Apps"); if (m_fontBold) ImGui::PopFont();
                            ImGui::TextWrapped("Uninstalls bundled Store apps via the native PackageManager API - no PowerShell. Scan to see which are installed, tick the ones to remove, then remove. Removal runs in the background.");
                            ImGui::Spacing();

                            bool busy = uwpScanning.load() || uwpRemoving.load();
                            if (busy) ImGui::BeginDisabled();
                            if (PremiumButton(uwpScanned.load() ? "Re-scan Installed Apps" : "Scan for Installed Apps", ImVec2(-1, globalBtnHeight))) {
                                uwpScanning = true;
                                // Guarded: an exception at a thread's entry point
                                // terminates the process, and this thread owns
                                // uwpScanning - a throw would also have left the
                                // whole Store-apps section disabled for good.
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
                            else if (uwpRemoving.load()) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Removing selected apps... (see Diagnostics > Logs)");

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
                    }
                }

                else if (activeTab == TAB_DIAGNOSTICS && diagSubTab == 0) {
                    ImGui::TextWrapped("Average FPS doesn't tell you about stutter. This 60-second test repeatedly polls a high-resolution timer and records the longest gaps, which is a good proxy for the driver interrupt delays behind frame time spikes.");
                    ImGui::Spacing();
                    if (localBenchmarking) {
                        float progress = (float)(currentTime - g_benchStartTime) / 60000.0f;
                        ImGui::TextColored(sexyPurple, "Measuring... Please do not move your mouse or start other programs.");
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, sexyPurple); ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f)); ImGui::PopStyleColor();
                    }
                    else {
                        // THE FIX: this test was reachable at any time, including with a
                        // game running. It calls GetMaxDPCSpikeMicroseconds in a tight
                        // loop for 60 seconds, and each call spins 10,000
                        // QueryPerformanceCounter iterations at
                        // THREAD_PRIORITY_TIME_CRITICAL - so pressing it mid-match pegs
                        // a full core at a priority above every thread the game has,
                        // for a minute, inside the application whose entire purpose is
                        // protecting that game's frame pacing. Same family as the
                        // background-throttling bug: the tool measurably hurting the
                        // thing it was turned on to help.
                        //
                        // Refused rather than de-prioritised. The time-critical spin is
                        // what makes the measurement mean anything, and a reading taken
                        // while a game saturates the CPU would not describe the idle
                        // system the results are presented as describing - so running
                        // it anyway would trade one honest failure for a plausible
                        // wrong number.
                        const bool gameBusy = g_isGameRunning.load();
                        if (gameBusy) ImGui::BeginDisabled();
                        bool startTest = PremiumButton("Run 60-Second Latency Test", ImVec2(-1, globalBtnHeight));
                        if (gameBusy) {
                            ImGui::EndDisabled();
                            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                                "Unavailable while a game is running - the test would take a whole CPU core at the\n"
                                "highest priority for a minute, and the reading would not be meaningful anyway.");
                        }
                        if (startTest && !gameBusy) {
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
                                    // Wall clock, kept so the finished run can ask what
                                    // else was happening during it. See below.
                                    const ULONGLONG wallStart =
                                        OptiMax::Diagnostics::Attribution::NowUtc();
                                    // Stops if a game launches part-way through, for the
                                    // same reason the button refuses to start with one
                                    // already running. Abandoning the samples rather
                                    // than publishing them is deliberate: a partial run
                                    // ending the moment a game loaded would report the
                                    // quietest 20 seconds of the minute as the result.
                                    bool gameInterrupted = false;
                                    while (GetTickCount64() - start < 60000 && g_isAppRunning) {
                                        if (g_isGameRunning.load()) { gameInterrupted = true; break; }
                                        localSamples.push_back(OptiMax::Profiler::GetMaxDPCSpikeMicroseconds());
                                    }
                                    if (gameInterrupted) {
                                        g_isBenchmarking = false;
                                        SafeAddLog("LATENCY TEST: Stopped - a game started, and continuing would have cost it a CPU core. No result was recorded; run it again once the game is closed.");
                                        return;
                                    }
                                    // v4.3.0: the result is presented as describing an
                                    // IDLE system. If a virus scan, a display reset or a
                                    // disk timeout happened during the minute, it does
                                    // not describe that at all - it describes a loaded
                                    // machine, shown as a baseline. That is the same
                                    // class as the timer warning that reported safe mode
                                    // as a failure: a plausible wrong number, which is
                                    // worse than no number, because these get quoted.
                                    //
                                    // Discarded rather than annotated, matching what the
                                    // game-interrupt path above already does. A single
                                    // retried disk I/O is below the bar and only
                                    // disclosed, so an ordinarily busy disk can still
                                    // produce a reading.
                                    const auto quality =
                                        OptiMax::Diagnostics::SelfGovernor::CheckMeasurementWindow(
                                            wallStart, OptiMax::Diagnostics::Attribution::NowUtc());
                                    if (quality.verdict ==
                                        OptiMax::Diagnostics::SelfGovernor::ReadingVerdict::Contaminated) {
                                        g_isBenchmarking = false;
                                        SafeAddLog("LATENCY TEST: Result " + quality.reason
                                            + " Run it again once the system is quiet.");
                                        return;
                                    }

                                    auto audit = OptiMax::Benchmark::CalculateAudit(localSamples);
                                    std::lock_guard<std::mutex> lock(g_benchMutex); g_lastAudit = audit; g_isBenchmarking = false;
                                    // Unknown is published but never described as verified
                                    // quiet - we could not look, and saying otherwise would
                                    // be the lie this whole release is about removing.
                                    if (quality.verdict ==
                                        OptiMax::Diagnostics::SelfGovernor::ReadingVerdict::Unknown) {
                                        SafeAddLog("LATENCY TEST: Complete - " + quality.reason + ".");
                                    }
                                    else {
                                        SafeAddLog("LATENCY TEST: Complete (" + quality.reason + ").");
                                    }
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
                        else ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Result: Inconsistent. Try the Performance and Interference sections on the Tweaks page, and check for outdated drivers.");
                        ImGui::EndChild();
                    }

                }

                else if (activeTab == TAB_DIAGNOSTICS && diagSubTab == 1) {
                    if (PremiumToggle("Show Live Readings", &isProfilerRunning)) {
                        SafeAddLog(isProfilerRunning ? "MONITOR: Live readings started." : "MONITOR: Live readings stopped.");
                    }
                    ImGui::Spacing();

                    // ==========================================================
                    // LIVE READINGS - five dials, then the 60-second history.
                    //
                    // Was five PremiumGauges (270-degree arcs, one purple, in
                    // ImGui::Columns) in a fixed 165px card. Now full RingDial-
                    // family dials with per-metric colour, and the history that
                    // was already being collected but never drawn - histCpu /
                    // histRam / histDpc have been filled every second since
                    // 4.3.0 and nothing on this page had ever shown them.
                    // ==========================================================
                    {
                        using namespace OptiMaxUi;

                        // ⚠️ TRIED AND REVERTED: making this collapsible, and folding
                        // it away once the first stall landed. It solved the layout
                        // problem - the dials and the history plot are ~450px
                        // between them and were squeezing Session Diagnostics to
                        // nothing - but it solved it by hiding the page's own
                        // headline content the moment the page became interesting,
                        // and a panel that disappears on you is worse than one you
                        // scroll past. The real fix is the height floor on Session
                        // Diagnostics below; this is left as a note so the idea is
                        // not re-had.
                        Eyebrow("LIVE READINGS  -  updates every second");

                        const float gap = ImGui::GetStyle().ItemSpacing.x;
                        const float colW = (ImGui::GetContentRegionAvail().x
                            - ImGui::GetStyle().WindowPadding.x * 2.0f - gap * 4.0f) / 5.0f;
                        // The ring is sized from the column so five of them always
                        // fit the width, rather than from a constant that only
                        // suits one window size.
                        const float dialR = ImMin(58.0f * dpiScale, colW * 0.44f);

                        const float dialLine = ImGui::GetTextLineHeight();
                        const float dialSub = ImGui::GetStyle().FontSizeBase * 0.80f;
                        ImGui::BeginChild("TelemetryDials",
                            ImVec2(0, dialR * 2.0f + 10.0f + dialLine + 2.0f + dialSub
                                + ImGui::GetStyle().WindowPadding.y * 2.0f + 4.0f),
                            true, staticCardFlags);

                        char v[24], s[40];

                        // Read once. GetSystemInfo is cheap but there is no reason
                        // to call it every frame.
                        static char dialCoreTxt[24] = { 0 };
                        if (!dialCoreTxt[0]) {
                            SYSTEM_INFO si{}; GetSystemInfo(&si);
                            sprintf_s(dialCoreTxt, "%lu cores", si.dwNumberOfProcessors);
                        }

                        sprintf_s(v, "%d", cachedCpuLoad);
                        sprintf_s(s, "%s", dialCoreTxt);
                        MetricDial("dCpu", cachedCpuLoad / 100.0f, v, "%", "CPU load", s,
                            dialR, m_fontNumber, kSigCpu);
                        ImGui::SameLine();

                        sprintf_s(v, "%d", cachedGpuLoad);
                        sprintf_s(s, "%s", cachedGpuLoad > 0 ? "in use" : "idle");
                        MetricDial("dGpu", cachedGpuLoad / 100.0f, v, "%", "GPU load", s,
                            dialR, m_fontNumber, kAccentBr);
                        ImGui::SameLine();

                        sprintf_s(v, "%d", cachedRam);
                        sprintf_s(s, "%d%% still free", 100 - ImClamp(cachedRam, 0, 100));
                        MetricDial("dRam", cachedRam / 100.0f, v, "%", "Memory", s,
                            dialR, m_fontNumber, kSigMem);
                        ImGui::SameLine();

                        // The 2000us full-scale is a display choice, not a limit -
                        // it is roughly where DPC latency stops being "fine" and
                        // starts being audible as stutter. The number is the fact;
                        // the arc is only a sense of where it sits.
                        sprintf_s(v, "%.0f", cachedDpcSpike);
                        sprintf_s(s, "%s", cachedDpcSpike < 500.0 ? "consistent" : "spiky");
                        MetricDial("dDpc", ImClamp((float)cachedDpcSpike / 2000.0f, 0.0f, 1.0f),
                            v, OMX_MICRO "s", "DPC latency", s,
                            dialR, m_fontNumber, kSigDpc);
                        ImGui::SameLine();

                        // Binary by nature: the timer is either at the resolution
                        // we asked for or it is not. A part-filled arc would imply
                        // a scale that does not exist, so it reads full or nearly
                        // empty and the caption carries the meaning.
                        const bool timerOk = (cachedTimer > 0.0 && cachedTimer <= 0.6);
                        sprintf_s(v, "%.2f", cachedTimer);
                        sprintf_s(s, "%s", timerOk ? "verified active" : "not lowered");
                        MetricDial("dTimer", timerOk ? 1.0f : 0.12f, v, "ms", "System timer", s,
                            dialR, m_fontNumber, timerOk ? kCalm : kDim);

                        ImGui::EndChild();

                        // ---- LAST 60 SECONDS ----
                        ImGui::Dummy(ImVec2(0, 4 * dpiScale));
                        Eyebrow("LAST 60 SECONDS");

                        static float hC[60], hM[60], hD[60];
                        int nC = 0, nM = 0, nD = 0;
                        histCpu.Snapshot(hC, nC);
                        histRam.Snapshot(hM, nM);
                        histDpc.Snapshot(hD, nD);

                        float dpcPeak = 0.0f;
                        for (int i = 0; i < nD; ++i) dpcPeak = ImMax(dpcPeak, hD[i]);

                        HistorySeries hsv[3];
                        // CPU and memory share a REAL 0-100 axis, so their heights
                        // are comparable with each other and with the gridlines.
                        hsv[0].vals = hC; hsv[0].count = nC; hsv[0].vmax = 100.0f;
                        hsv[0].col = kSigCpu; hsv[0].fill = true;
                        hsv[0].label = "CPU"; hsv[0].unit = "%";
                        hsv[1].vals = hM; hsv[1].count = nM; hsv[1].vmax = 100.0f;
                        hsv[1].col = kSigMem; hsv[1].label = "Memory"; hsv[1].unit = "%";
                        // DPC is microseconds and CANNOT share that axis, so it is
                        // scaled to its own peak - which the legend says outright,
                        // because an unlabelled mixed axis invites exactly the
                        // comparison it cannot support.
                        hsv[2].vals = hD; hsv[2].count = nD; hsv[2].vmax = 0.0f;
                        hsv[2].col = kSigDpc; hsv[2].label = "DPC"; hsv[2].unit = OMX_MICRO "s";

                        const float plotH = 132.0f * dpiScale;
                        ImGui::BeginChild("HistCard",
                            ImVec2(0, plotH + ImGui::GetTextLineHeight() * 2.0f
                                + ImGui::GetStyle().WindowPadding.y * 2.0f
                                + ImGui::GetStyle().ItemSpacing.y * 2.0f),
                            true, staticCardFlags);

                        if (nC < 2 && nM < 2) {
                            // Says which it is. A blank plot is ambiguous between
                            // "nothing has happened" and "nothing is collecting",
                            // which is the same ambiguity the watchdog's proof-of-
                            // life count exists to remove.
                            ImGui::Dummy(ImVec2(0, plotH * 0.45f));
                            const char* waitMsg = "Collecting - the first readings appear within a few seconds.";
                            const float ww = ImGui::CalcTextSize(waitMsg).x;
                            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ww) * 0.5f);
                            ImGui::TextDisabled("%s", waitMsg);
                            ImGui::Dummy(ImVec2(0, plotH * 0.45f));
                        }
                        else {
                            HistoryPlot("hist", hsv, 3,
                                ImVec2(ImGui::GetContentRegionAvail().x, plotH));

                            ImGui::TextDisabled("60s ago");
                            ImGui::SameLine();
                            const char* nowLbl = "now";
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                                + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(nowLbl).x);
                            ImGui::TextDisabled("%s", nowLbl);
                        }

                        LegendKey("CPU", kSigCpu);
                        ImGui::SameLine(0, 18 * dpiScale);
                        LegendKey("Memory", kSigMem);
                        ImGui::SameLine(0, 18 * dpiScale);
                        {
                            char dpcKey[64];
                            sprintf_s(dpcKey, "DPC latency (own scale, peak %.0f%ss)",
                                dpcPeak, OMX_MICRO);
                            LegendKey(dpcKey, kSigDpc);
                        }
                        ImGui::EndChild();
                    }

                    // --- SESSION DIAGNOSTICS (v4.3.0) ---
                    // On Live Monitoring rather than under the latency test. Someone
                    // whose game just stuttered looks where the system readings are,
                    // not under a tab named after a 60-second benchmark - and this
                    // is the headline of the release, so it should not need finding.
                    ImGui::Dummy(ImVec2(0, 6 * dpiScale)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 6 * dpiScale));
                    if (m_fontBold) ImGui::PushFont(m_fontBold);
                    ImGui::TextColored(sexyPurple, "Session Diagnostics");
                    if (m_fontBold) ImGui::PopFont();
                    ImGui::TextWrapped("OptiMax watches for moments the whole system stops responding and "
                        "checks the Windows event log to say what caused them. This is the thing to send "
                        "if you are reporting a stutter.");
                    ImGui::Spacing();

                    // 🔴 A FLOOR, BECAUSE A NEGATIVE HEIGHT CAN REACH ZERO.
                    //
                    // This asked for "everything left, minus the button", which is
                    // right while there IS something left. Once the dials and the
                    // history plot went in above it - about 450px between them -
                    // there was not, and ImGui clamped this panel to nothing. The
                    // stall rows and their advice did not scroll off, they ceased
                    // to render: a 1px sliver with a Copy Report button under it,
                    // on the page whose entire purpose is those rows.
                    //
                    // Taking the larger of "what is left" and a real minimum means
                    // the worst case is now the PAGE scrolling, which is visible
                    // and recoverable, instead of the panel silently vanishing.
                    // The minimum is derived so it holds a summary line plus two
                    // stalls complete with their advice and apply buttons.
                    //
                    // 🔴 THIS IS THE WHOLE FIX. Collapsing the dials above was
                    // tried and reverted (see the note there), so nothing else is
                    // keeping this panel on screen - do not remove the ImMax and
                    // go back to a bare negative height, however tidy it looks.
                    const float diagMinH =
                        ImGui::GetTextLineHeight() * 10.0f + globalBtnHeight * 2.0f;
                    const float diagFitH =
                        ImGui::GetContentRegionAvail().y
                        - (globalBtnHeight + ImGui::GetStyle().ItemSpacing.y * 2);
                    ImGui::BeginChild("SessionDiag",
                        ImVec2(0, ImMax(diagMinH, diagFitH)),
                        true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                    {
                        const int stalls = OptiMax::Diagnostics::g_stallCount.load();
                        const auto records = OptiMax::Diagnostics::SnapshotStalls();

                        const unsigned long long wdChecks =
                            OptiMax::Diagnostics::g_watchdogChecks.load();

                        if (wdChecks == 0) {
                            // Shown INSTEAD of a verdict, not alongside one. "No
                            // stalls detected" from a watchdog that never ran is not
                            // reassurance, it is a false negative wearing the same
                            // words - which is the exact failure this feature exists
                            // to remove.
                            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                                "The stall watchdog is not running - stall detection is "
                                "unavailable this session.");
                        }
                        else if (stalls == 0) {
                            // The reassuring line, stated explicitly. Its absence is
                            // what made the original freeze report unanswerable. The
                            // watchdog's own count is shown beside it so the claim is
                            // checkable rather than trusted.
                            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
                                "No stalls detected this session.");
                            ImGui::TextDisabled("%llu stall checks, %llu background checks.",
                                wdChecks, OptiMax::Diagnostics::g_tickCount.load());
                        }
                        else {
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                                "%d stall%s this session, worst %llums",
                                stalls, stalls == 1 ? "" : "s",
                                (unsigned long long)OptiMax::Diagnostics::g_worstStallMs.load());
                            ImGui::Spacing();
                            int stallRowId = 0;
                            for (const auto& r : records) {
                                ImGui::PushID(stallRowId++);
                                ImGui::Bullet();
                                ImGui::SameLine();
                                ImGui::TextWrapped("%s  -  held off %llums%s  -  %s",
                                    OptiMax::Diagnostics::FormatLocalTime(r.whenUtc).c_str(),
                                    (unsigned long long)r.lateMs,
                                    r.gameRunning ? " while a game was running" : "",
                                    r.cause.c_str());

                                // ---- WHAT TO DO ABOUT IT ----
                                // Inline, under the cause it belongs to. A cause
                                // with no next step is a better error message, not
                                // a fix - this is the half that helps.
                                //
                                // Nothing is shown for a rate-limited stall: no
                                // lookup ran, so there is no classification to
                                // advise on, and advising from the PREVIOUS
                                // stall's cause is the same fabrication the cause
                                // string itself refuses to make.
                                if (!r.rateLimited) {
                                    namespace Rem = OptiMax::Diagnostics::Remedy;
                                    const Rem::Advice adv = r.attributed
                                        ? Rem::ForCause(r.kind)
                                        : Rem::ForUnattributed(r.allProbesRan);

                                    ImGui::Indent(22.0f * dpiScale);
                                    ImGui::TextColored(ImVec4(0.78f, 0.42f, 1.0f, 1.0f),
                                        "%s", adv.headline);
                                    ImGui::PushStyleColor(ImGuiCol_Text,
                                        ImGui::ColorConvertU32ToFloat4(OptiMaxUi::kDim));
                                    ImGui::TextWrapped("%s", adv.detail);
                                    ImGui::PopStyleColor();

                                    if (adv.Actionable()) {
                                        const bool done = RemedyAlreadySatisfied(adv.action);
                                        if (done) {
                                            // Says so rather than offering a button
                                            // that would do nothing - a no-op the
                                            // user cannot tell from a failure is
                                            // the defect class this release exists
                                            // to remove.
                                            ImGui::TextDisabled("Already on.%s",
                                                adv.needsReboot ? " Restart for it to take effect." : "");
                                        }
                                        else {
                                            if (PremiumButton(Rem::ActionLabel(adv.action),
                                                ImVec2(320.0f * dpiScale, globalBtnHeight))) {
                                                const bool ok = ApplyRemedy(adv.action, localAntiCheatActive);
                                                SafeAddLog(std::string("REMEDY: ")
                                                    + (ok ? "Applied " : "FAILED to apply ")
                                                    + Rem::ActionLogName(adv.action)
                                                    + " from a stall recommendation.");
                                            }
                                            if (adv.needsReboot) {
                                                ImGui::TextColored(ImVec4(0.96f, 0.65f, 0.14f, 1.0f),
                                                    "Windows only reads this at start-up - it cannot help "
                                                    "the stall above, only later ones.");
                                            }
                                        }
                                    }
                                    ImGui::Unindent(22.0f * dpiScale);
                                    ImGui::Spacing();
                                }
                                ImGui::PopID();
                            }
                            if ((int)records.size() < stalls) {
                                ImGui::TextDisabled("...and %d more, not listed individually.",
                                    stalls - (int)records.size());
                            }
                        }

                        const int slowOwn = OptiMax::Diagnostics::g_slowWorkCount.load();
                        if (slowOwn > 0) {
                            ImGui::Spacing();
                            // Kept visibly separate from the stall count. Folding
                            // OptiMax's own cost into the system's would be the app
                            // quietly blaming Windows for its own work.
                            ImGui::TextDisabled("OptiMax's own checks ran long %d time%s.",
                                slowOwn, slowOwn == 1 ? "" : "s");
                        }
                    }
                    ImGui::EndChild();

                    // One string for both the screen and the clipboard - see the note
                    // above BuildSessionReport. Two renderings of the same facts drift
                    // apart; this project has the scars.
                    if (PremiumButton("Copy Report for Support", ImVec2(-1, globalBtnHeight))) {
                        ImGui::SetClipboardText(
                            OptiMax::Diagnostics::BuildSessionReport(MakeReportContext()).c_str());
                        SafeAddLog("DIAGNOSTICS: Session report copied to the clipboard.");
                    }
                }

                else if (activeTab == TAB_SETTINGS) {
                    // THE FIX: staticCardFlags disables scrolling (shared by every card
                    // in the app, most of which are fixed-height and don't need it).
                    // Settings has grown enough - RAM cleaner section, Profile export/
                    // import, Danger Zone - that its content can exceed the window
                    // height when not maximized, with no way to reach what's cut off.
                    // This is the only BeginChild changed; every other card keeps the
                    // shared no-scroll flags untouched.
                    ImGui::BeginChild("SettingsBox", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

                    // ---- PREFERENCES: rows, not a stack of bare switches ----
                    // These were five PremiumToggles with the whole explanation
                    // crammed into the label - "Disable Multi-Plane Overlay (fixes
                    // some flickering)" is a label doing a tooltip's job, and it
                    // still had nowhere to say that the fix needs a restart. Same
                    // treatment as the Dashboard and the Tweaks page: a short
                    // title, a subtitle that says the current state, a chip, and
                    // the real explanation on hover.
                    using namespace OptiMaxUi;
                    Eyebrow("PREFERENCES  -  hover a row for what it actually does");
                    {
                        const int prefRows = 6;
                        ImGui::BeginChild("PrefRows",
                            ImVec2(0, prefRows * ToggleRowPitch() + ImGui::GetStyle().WindowPadding.y * 2.0f),
                            true, staticCardFlags);
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

                        bool v;

                        v = g_settingAutoStart.load();
                        if (ToggleRow(OMX_ICON_PLAY, "Start with Windows",
                            v ? "Starts minimised to the tray at logon"
                              : "OptiMax only runs when you open it",
                            v ? "ON" : "OFF", v ? ChipKind::On : ChipKind::Off, &v,
                            "Registers a scheduled task that starts OptiMax minimised to the tray when "
                            "you log in, so your tweaks are re-applied without you opening it. Needs "
                            "administrator rights to register; if it is refused, the preference is still "
                            "remembered and retried at the next launch.")) {
                            g_settingAutoStart = v; SavePersistentState();
                            // SetStartupRegistry reports the real result of the
                            // schtasks call, including refusing for want of
                            // administrator rights. A flat success line used to
                            // follow it regardless, so a refused registration
                            // produced a warning and a confirmation back to back.
                            SetStartupRegistry(v);
                        }

                        // Reflects whether "Minimize to Tray" was remembered from the
                        // close dialog (g_closeBehavior == 1). Turning it off resets
                        // it to asking every time; turning it on sets the same
                        // remembered behaviour the dialog itself sets, without having
                        // to close the app first.
                        v = (g_closeBehavior.load() == 1);
                        if (ToggleRow(OMX_ICON_GRID, "Close to tray",
                            v ? "The close button minimises instead of exiting"
                              : "The close button asks what to do",
                            v ? "ON" : "OFF", v ? ChipKind::On : ChipKind::Off, &v,
                            "With this on, the window's close button sends OptiMax to the tray rather "
                            "than exiting, and it stops asking. Right-click the tray icon and choose Exit "
                            "to actually quit. Minimise is unaffected - it has always been a normal "
                            "minimise.")) {
                            g_closeBehavior = v ? 1 : 0;
                            SavePersistentState();
                            SafeAddLog(v
                                ? "SETTINGS: Closing will now always minimize to tray. Use the tray icon's right-click menu to Exit."
                                : "SETTINGS: Closing will ask each time again.");
                        }

                        v = g_settingSafeMode.load();
                        if (ToggleRow(OMX_ICON_SPARK, "Restore point before tweaks",
                            v ? "A restore point is created before risky changes"
                              : "No restore point is created",
                            v ? "ON" : "OFF", v ? ChipKind::On : ChipKind::Off, &v,
                            "Asks Windows for a System Restore point before the tweaks that touch boot "
                            "configuration, VBS, MSI mode or GPU interrupts. Windows rate-limits restore "
                            "points, so one is not always created - and it protects the system "
                            "configuration, not your files.")) {
                            g_settingSafeMode = v; SavePersistentState();
                            SafeAddLog(v ? "SETTINGS: Restore points enabled." : "SETTINGS: Restore points disabled.");
                        }

                        // REBOOT rather than ON: the registry value is written
                        // immediately but the display driver only reads it at start-up,
                        // so a two-state switch would claim an effect that is not there
                        // yet. Same treatment as GPU interrupts on the Dashboard.
                        v = g_settingMpoDisable.load();
                        if (ToggleRow(OMX_ICON_DIAMOND, "Disable Multi-Plane Overlay",
                            "Fixes flickering and black flashes on some GPUs",
                            v ? "REBOOT" : "OFF", v ? ChipKind::Pending : ChipKind::Off, &v,
                            "MPO lets the GPU compose overlays directly. On some driver and monitor "
                            "combinations it causes flickering, black flashes or stutter in windowed "
                            "games. Turning it off is a known workaround. Written immediately, but "
                            "Windows only reads it at start-up - restart for it to take effect.")) {
                            // The registry write is what makes this real; the saved
                            // preference only ever recorded the switch position.
                            // Ordered so the setting is not remembered as applied if
                            // applying it failed.
                            if (OptiMax::Profiler::SetMPODisabled(v)) {
                                g_settingMpoDisable = v; SavePersistentState();
                                SafeAddLog(v
                                    ? "SETTINGS: Multi-Plane Overlay disabled - restart Windows for it to take effect."
                                    : "SETTINGS: Multi-Plane Overlay restored - restart Windows for it to take effect.");
                            }
                            else {
                                SafeAddLog("SETTINGS WARNING: Could not change the Multi-Plane Overlay setting - "
                                    "this needs administrator rights.");
                            }
                        }

                        // The opt-in for automatic remedies. Default off, and the
                        // subtitle states what it will and will not do rather than
                        // leaving that to the tooltip - this is the one setting in
                        // the app that lets it change the system without a click,
                        // so the consent has to be informed at the point of giving
                        // it, not one hover away.
                        v = g_settingAutoRemedy.load();
                        if (ToggleRow(OMX_ICON_SPARK, "Auto-fix stalls",
                            v ? "Applies safe, instant tweaks when a stall is explained"
                              : "Stalls are explained but nothing is applied for you",
                            v ? "ON" : "OFF", v ? ChipKind::On : ChipKind::Off, &v,
                            "When OptiMax detects a stall and can name its cause, it applies the "
                            "matching tweak without asking.\n\n"
                            "Only tweaks that are reversible and take effect immediately are "
                            "eligible - nothing that needs a restart, nothing that changes your "
                            "security settings, and nothing while an anti-cheat is running. "
                            "Every automatic change is written to the log.\n\n"
                            "Most stalls are caused by Defender scans, and OptiMax will not "
                            "reconfigure Defender for you - so this does less than it sounds "
                            "like, on purpose.")) {
                            g_settingAutoRemedy = v; SavePersistentState();
                            SafeAddLog(v
                                ? "SETTINGS: Automatic stall remedies enabled - safe, instant tweaks will be applied without asking."
                                : "SETTINGS: Automatic stall remedies disabled.");
                        }

                        // Live here now, not a disabled mirror. It was read-only so
                        // that the Dashboard was unambiguously "in charge" - but the
                        // Tweaks page carries it too, so there were already two live
                        // copies and a dead third one sitting directly above its own
                        // threshold slider was the only confusing part left.
                        v = g_settingAutoRam.load();
                        {
                            char sub[96];
                            if (g_IsLowSpec) sprintf_s(sub, "Unavailable - limited hardware detected");
                            else sprintf_s(sub, "Trims background apps above %d%% memory",
                                g_ramCleanThreshold.load());
                            if (ToggleRow(OMX_ICON_MEM, "Automatic RAM cleaning", sub,
                                g_IsLowSpec ? "N/A" : (v ? "ON" : "OFF"),
                                g_IsLowSpec ? ChipKind::Unavailable : (v ? ChipKind::On : ChipKind::Off),
                                &v,
                                "Trims background programs' memory when usage passes the threshold below. "
                                "Your game is never touched, and cleaning is deferred while the system is "
                                "already struggling. The same switch appears on the Dashboard and under "
                                "Tweaks > Privacy & cleanup.",
                                !g_IsLowSpec)) {
                                g_settingAutoRam = v; SavePersistentState();
                            }
                        }

                        ImGui::PopStyleVar();
                        ImGui::EndChild();
                    }

                    const bool t4 = g_settingAutoRam.load() && !g_IsLowSpec;
                    ImGui::Spacing();
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

                    {
                        OptiMaxUi::ModalStyle _modalStyle;
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
                    }

                    ImGui::EndChild();
                }

                else if (activeTab == TAB_DIAGNOSTICS && diagSubTab == 2) {
                    ImGui::BeginChild("LogArea", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                    std::lock_guard<std::mutex> lock(g_logMutex);
                    for (auto& l : g_logs) { ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%s", l.c_str()); }
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) { ImGui::SetScrollHereY(1.0f); }
                    ImGui::EndChild();
                }

                else if (activeTab == TAB_DISCORD) {
                    static OptiMax::Discord::DebloatOptions dopt;   // defaults come from the struct
                    static bool discordRemoveLocalStorage = false;
                    static std::string discordStatus;
                    ImVec4 warnColor(1.0f, 0.7f, 0.2f, 1.0f);
                    ImVec4 okColor(0.4f, 0.85f, 0.5f, 1.0f);

                    ImGui::TextWrapped("Slims down your Discord install using native file and registry operations only - no scripts are run. Discord must be fully closed (including the tray icon) before debloating or writing settings.");
                    ImGui::Spacing();

                    // --- Warning / notes box ---
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.10f, 0.02f, 1.0f));
                    // Title plus three paragraphs that each wrap to about two lines.
                    // At 118px this scrolled - it has default flags so nothing was
                    // lost, but a "before you run this" warning that needs scrolling
                    // to finish reading is a warning people do not finish reading.
                    {
                        float notesTitleH = ImGui::GetTextLineHeight();
                        if (m_fontBold) {
                            ImGui::PushFont(m_fontBold);
                            notesTitleH = ImGui::GetTextLineHeight();
                            ImGui::PopFont();
                        }
                        ImGui::BeginChild("DiscordNotes",
                            ImVec2(0, ImGui::GetStyle().WindowPadding.y * 2.0f
                                + notesTitleH
                                + ImGui::GetTextLineHeight() * 6.0f
                                + ImGui::GetStyle().ItemSpacing.y * 4.0f),
                            true);
                    }
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
                    // THE FIX: this ran RunDebloat() on the UI thread. With "Create
                    // backup to Desktop first" ticked that is a 527 MB folder copy
                    // inside the render loop - the window stops painting, Windows
                    // greys it out and titles it "Not Responding", and there is no
                    // feedback of any kind until it finishes. Every user who ticked
                    // the one option that protects them concluded the app had
                    // crashed, which is the worst possible thing for that option.
                    //
                    // Same shape as the UWP scan/remove and Health Repair threads
                    // above: detached worker, guarded body (an exception at a thread
                    // entry point calls std::terminate), and the flag it owns is
                    // cleared on every path so the button can never stay disabled.
                    const bool discordBusy = OptiMax::Discord::Progress::g_running.load();
                    if (discordBusy) ImGui::BeginDisabled();
                    if (PremiumButton("Run Discord Debloat", ImVec2(-1, globalBtnHeight))) {
                        if (OptiMax::Discord::IsRunning()) discordStatus = "Discord is still running - close it fully (including the tray icon) and try again.";
                        else {
                            discordStatus.clear();
                            OptiMax::Discord::Progress::g_running = true;
                            OptiMax::Discord::Progress::Set("Starting...", 0);
                            // dopt by value: the UI thread keeps editing the static
                            // as the user ticks boxes, and a run must use the options
                            // that were set when it started.
                            OptiMax::Discord::DebloatOptions snapshot = dopt;
                            std::thread([snapshot]() {
                                try {
                                    bool ok = OptiMax::Discord::RunDebloat(snapshot);
                                    OptiMax::Discord::Progress::Set(ok
                                        ? "Finished - see Diagnostics > Logs for details."
                                        : "Nothing was done - no Discord install was found, or the backup you asked for could not be made.", ok ? 100 : -1);
                                }
                                catch (...) {
                                    SafeAddLog("DISCORD ERROR: The debloat failed unexpectedly. Your install may be partly modified - check the log.");
                                    OptiMax::Discord::Progress::Set("Failed unexpectedly - see Diagnostics > Logs.", -1);
                                }
                                OptiMax::Discord::Progress::g_running = false;
                                }).detach();
                        }
                    }
                    if (discordBusy) ImGui::EndDisabled();

                    // Progress line. The backup is the only stage with a meaningful
                    // total, so the bar appears for that and the rest report a stage
                    // name - a bar that jumps 0 to 100 in a second is worse than none.
                    {
                        std::string stage = OptiMax::Discord::Progress::Stage();
                        if (!stage.empty()) {
                            int pct = OptiMax::Discord::Progress::g_percent.load();
                            ImGui::Spacing();
                            ImGui::TextColored(discordBusy ? warnColor : okColor, "%s", stage.c_str());
                            if (discordBusy && pct >= 0) {
                                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, sexyPurple);
                                ImGui::ProgressBar(pct / 100.0f, ImVec2(-1.0f, 0.0f));
                                ImGui::PopStyleColor();
                            }
                        }
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

                    // Blocked while a debloat is in flight: both touch the same install
                    // tree, and settings.json is inside the folder being copied.
                    if (discordBusy) ImGui::BeginDisabled();
                    if (PremiumButton("Apply Optimized Settings", ImVec2(-1, globalBtnHeight))) {
                        if (OptiMax::Discord::IsRunning()) discordStatus = "Close Discord first - it overwrites settings.json on exit.";
                        else if (OptiMax::Discord::WriteOptimizedSettings()) discordStatus = "OK - Optimized settings.json written.";
                        else discordStatus = "Could not write settings.json - is Discord installed and closed?";
                    }
                    if (discordBusy) ImGui::EndDisabled();

                    ImGui::Dummy(ImVec2(0, 8 * dpiScale)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 8 * dpiScale));

                    // ================= SECTION 3: CLEAN CACHE =================
                    if (m_fontBold) ImGui::PushFont(m_fontBold); ImGui::TextColored(sexyPurple, "3. Clean Cache"); if (m_fontBold) ImGui::PopFont();
                    ImGui::TextWrapped("Clears accumulated runtime data (Cache, GPUCache, ShaderCache, Crashpad, logs, and more). Your account session is preserved unless you remove Local Storage.");
                    ImGui::Spacing();
                    ImGui::PushID("dcache");
                    PremiumToggle("Also remove Local Storage (frees more space, signs you out)", &discordRemoveLocalStorage);
                    ImGui::PopID();
                    ImGui::Spacing();
                    if (discordBusy) ImGui::BeginDisabled();
                    if (PremiumButton("Clean Discord Cache", ImVec2(-1, globalBtnHeight))) {
                        if (OptiMax::Discord::IsRunning()) discordStatus = "Close Discord first so its cache files aren't locked.";
                        else { OptiMax::Discord::CleanCache(discordRemoveLocalStorage); discordStatus = "OK - Cache cleaned. See Diagnostics > Logs for details."; }
                    }
                    if (discordBusy) ImGui::EndDisabled();
                }

                // ---- THROTTLING CONFIRMATION ----
                // Raised from the Dashboard control row or the Tweaks page's
                // Interference section, and declared once here at page level so
                // both routes open the same popup id. It also sits outside the
                // Dashboard's !isAdmin BeginDisabled block, where it used to be -
                // a disabled modal cannot be dismissed.
                if (wantThrottleConfirm) ImGui::OpenPopup("Throttle Background Apps?");

                {
                    OptiMaxUi::ModalStyle _modalStyle;
                if (ImGui::BeginPopupModal("Throttle Background Apps?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "This affects every background app, not just games.");
                    ImGui::Separator(); ImGui::Spacing();
                    ImGui::TextWrapped("Background apps will be set to Below Normal CPU priority, Very Low disk I/O priority, and lowest memory page priority. Windows components and anti-cheat processes are left alone.");
                    ImGui::TextWrapped("Downloads, backups, streaming software, and file transfers may run noticeably slower while this is on.");
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    if (PremiumButton("Start Throttling", ImVec2(200 * dpiScale, globalBtnHeight))) {
                        // Skip whatever is being played - see EnableExtremeMode.
                        OptiMax::EnableExtremeMode(localAntiCheatActive, GetActiveGameExe());
                        SavePersistentState();
                        SafeAddLog("SYSTEM: Background app throttling turned on.");
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100 * dpiScale, globalBtnHeight))) {
                        // The row set the flag optimistically so the switch moved
                        // with the click; cancelling has to put it back, or the UI
                        // claims throttling is on when nothing was throttled.
                        g_isolationActive = false;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
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
