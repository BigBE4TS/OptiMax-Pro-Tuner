#pragma once
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <atomic>
#include "OptiMaxJson.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

extern void SafeAddLog(const std::string& msg);

// ===========================================================================
// DISCORD DEBLOAT
//
// Everything here is native filesystem / registry work - no PowerShell, no
// batch scripts, nothing spawned - so it doesn't add to OptiMax's AV surface.
// Discord lives in two places:
//   - %LOCALAPPDATA%\Discord   : the app-x.x.x install, Update.exe, packages
//   - %APPDATA%\discord        : settings.json and the browser caches
// Deletions are best-effort: a missing file is not an error (different Discord
// builds ship different modules), and every std::filesystem call uses the
// error_code overload so a locked/absent file can never throw across the UI.
// ===========================================================================

namespace OptiMax {
    namespace Discord {

        namespace fs = std::filesystem;

        // ----- progress reporting --------------------------------------------
        // RunDebloat is driven from a background thread (see the Discord tab in
        // OptiMaxApp.h). It has to be: the optional backup copies the entire
        // install - 527 MB on an ordinary machine - and doing that on the UI
        // thread froze the window for the whole copy with no feedback at all.
        // Every user who ticked "Create backup to Desktop first" reasonably
        // concluded the app had crashed, which made the one option that exists
        // to protect them the one most likely to be avoided.
        namespace Progress {
            inline std::mutex g_mutex;
            inline std::string g_stage;
            inline std::atomic<bool> g_running{ false };
            inline std::atomic<int>  g_percent{ -1 };   // -1 = no measurable total

            inline void Set(const std::string& stage, int percent = -1) {
                { std::lock_guard<std::mutex> lk(g_mutex); g_stage = stage; }
                g_percent = percent;
            }
            inline std::string Stage() {
                std::lock_guard<std::mutex> lk(g_mutex);
                return g_stage;
            }
        }

        struct DebloatOptions {
            bool backup = false;            // copy whole install to Desktop first
            bool removeKrisp = false;       // Krisp noise suppression module
            bool removeGameSDK = true;      // discord_game_sdk_*.dll
            bool removeRPC = false;         // Game Presence / RPC modules
            bool removeUpdater = true;      // Update.exe / Squirrel / RELEASES / packages
            bool disableAutostart = true;   // registry Run keys
            bool disableFSO = true;         // Fullscreen Optimizations for Discord.exe
            bool removeJunk = true;         // .sig, SwiftShader, Vulkan, d3dcompiler
            bool keepEnglish = true;        // keep en-*.pak language packs
            bool keepFrench = false;        // keep fr.pak language pack
        };

        // ----- path helpers ---------------------------------------------------
        inline fs::path EnvPath(const wchar_t* var, const wchar_t* leaf) {
            wchar_t* base = nullptr; size_t len = 0;
            if (_wdupenv_s(&base, &len, var) != 0 || !base) return fs::path();
            fs::path p(base); free(base);
            p /= leaf;
            return p;
        }

        inline fs::path RoamingDir() { return EnvPath(L"APPDATA", L"discord"); }        // settings + cache
        inline fs::path LocalDir() { return EnvPath(L"LOCALAPPDATA", L"Discord"); }     // install

        inline fs::path DesktopDir() {
            wchar_t buf[MAX_PATH] = { 0 };
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, buf))) return fs::path(buf);
            return fs::path();
        }

        inline bool IsInstalled() {
            std::error_code ec;
            return fs::exists(LocalDir(), ec) || fs::exists(RoamingDir(), ec);
        }

        inline bool IsRunning() {
            bool running = false;
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe = { sizeof(pe) };
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        if (_wcsicmp(pe.szExeFile, L"Discord.exe") == 0) { running = true; break; }
                    } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }
            return running;
        }

        // ----- small filesystem wrappers -------------------------------------
        inline bool SafeRemove(const fs::path& p) {
            std::error_code ec;
            if (!fs::exists(p, ec)) return false;
            fs::remove(p, ec);
            return !ec;
        }
        inline uintmax_t SafeRemoveAll(const fs::path& p) {
            std::error_code ec;
            if (!fs::exists(p, ec)) return 0;
            uintmax_t n = fs::remove_all(p, ec);
            return ec ? 0 : n;
        }

        // Parse "app-1.0.9028" into a comparable numeric tuple.
        inline std::vector<int> ParseVersion(const std::wstring& folderName) {
            std::vector<int> parts;
            size_t dash = folderName.find(L'-');
            if (dash == std::wstring::npos) return parts;
            std::wstring ver = folderName.substr(dash + 1);
            std::wstringstream ss(ver);
            std::wstring seg;
            while (std::getline(ss, seg, L'.')) {
                try { parts.push_back(std::stoi(seg)); }
                catch (...) { parts.push_back(0); }
            }
            return parts;
        }

        inline bool VersionLess(const std::vector<int>& a, const std::vector<int>& b) {
            size_t n = (std::max)(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) {
                int av = i < a.size() ? a[i] : 0;
                int bv = i < b.size() ? b[i] : 0;
                if (av != bv) return av < bv;
            }
            return false;
        }

        // Returns the newest app-x.x.x directory under %LOCALAPPDATA%\Discord.
        inline fs::path LatestAppDir() {
            fs::path root = LocalDir();
            std::error_code ec;
            fs::path latest; std::vector<int> latestVer;
            if (!fs::exists(root, ec)) return latest;
            for (auto& e : fs::directory_iterator(root, ec)) {
                if (ec) break;
                if (!e.is_directory(ec)) continue;
                std::wstring name = e.path().filename().wstring();
                if (name.rfind(L"app-", 0) != 0) continue;
                std::vector<int> v = ParseVersion(name);
                if (latest.empty() || VersionLess(latestVer, v)) { latest = e.path(); latestVer = v; }
            }
            return latest;
        }

        // ----- shortcut ("Discord Debloated.lnk") ----------------------------
        // THE FIX (A4-3): ppf->Save()'s HRESULT was discarded and the success line
        // logged regardless, so a failed save sent the user hunting the Desktop for
        // a shortcut that was never written.
        //
        // THE FIX (A4-6): CoUninitialize() was called only for S_OK. S_FALSE means
        // COM was already initialised on this thread - which is the normal case here,
        // since this runs on the UI thread - and it still increments the reference
        // count, so it still needs a matching CoUninitialize. Only an outright
        // failure (e.g. RPC_E_CHANGED_MODE) must not be balanced.
        inline bool CreateDebloatedShortcut(const fs::path& target) {
            fs::path desktop = DesktopDir();
            if (desktop.empty() || target.empty()) return false;
            fs::path lnk = desktop / L"Discord Debloated.lnk";

            HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            bool created = false;

            IShellLinkW* psl = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl)) && psl) {
                psl->SetPath(target.wstring().c_str());
                psl->SetWorkingDirectory(target.parent_path().wstring().c_str());
                psl->SetDescription(L"Discord (debloated - launches directly, skips update check)");
                IPersistFile* ppf = nullptr;
                if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)) && ppf) {
                    created = SUCCEEDED(ppf->Save(lnk.wstring().c_str(), TRUE));
                    ppf->Release();
                }
                psl->Release();
            }

            if (SUCCEEDED(hrInit)) CoUninitialize();

            SafeAddLog(created
                ? "DISCORD: Created 'Discord Debloated' shortcut on the Desktop."
                : "DISCORD WARNING: Could not create the 'Discord Debloated' Desktop shortcut - launch Discord.exe from its install folder instead.");
            return created;
        }

        // ----- individual debloat steps --------------------------------------
        inline void RemoveOldVersions(const fs::path& keep) {
            fs::path root = LocalDir();
            std::error_code ec;
            if (!fs::exists(root, ec)) return;
            int removed = 0;
            for (auto& e : fs::directory_iterator(root, ec)) {
                if (ec) break;
                if (!e.is_directory(ec)) continue;
                std::wstring name = e.path().filename().wstring();
                if (name.rfind(L"app-", 0) != 0) continue;
                if (e.path() == keep) continue;
                if (SafeRemoveAll(e.path()) > 0) removed++;
            }
            if (removed) SafeAddLog("DISCORD: Removed " + std::to_string(removed) + " old version folder(s).");
        }

        inline void RemoveLanguagePacks(const fs::path& appDir, bool keepEnglish, bool keepFrench) {
            fs::path locales = appDir / L"locales";
            std::error_code ec;
            if (!fs::exists(locales, ec)) return;

            // THE FIX: both toggles are freely unticked in the UI ("Language packs to
            // keep: Keep English / Keep French"), and with both off this loop deleted
            // EVERY .pak in locales\. Discord is an Electron app - with no locale pack
            // at all it comes up with no text in it. Nothing warned about that, and it
            // is not what "remove unused language packs" means to anyone reading it.
            // English is the floor: it is Discord's own fallback locale, so keeping it
            // is what makes the rest safe to remove.
            if (!keepEnglish && !keepFrench) {
                keepEnglish = true;
                SafeAddLog("DISCORD: Keeping the English language pack - removing every locale would leave Discord with no text at all.");
            }

            int removed = 0;
            for (auto& e : fs::directory_iterator(locales, ec)) {
                if (ec) break;
                std::wstring fn = e.path().filename().wstring();
                if (e.path().extension() != L".pak") continue;
                bool keep = false;
                if (keepEnglish && fn.rfind(L"en", 0) == 0) keep = true;
                if (keepFrench && fn.rfind(L"fr", 0) == 0) keep = true;
                if (!keep && SafeRemove(e.path())) removed++;
            }
            if (removed) SafeAddLog("DISCORD: Removed " + std::to_string(removed) + " unused language pack(s).");
        }

        // Recursively delete files/dirs under 'root' whose name contains 'needle'
        // (case-insensitive). Used for module folders and DLL patterns.
        inline int PurgeByName(const fs::path& root, const std::wstring& needle) {
            std::error_code ec;
            if (!fs::exists(root, ec)) return 0;
            std::wstring low = needle;
            std::transform(low.begin(), low.end(), low.begin(), ::towlower);
            std::vector<fs::path> hits;
            for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
                it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                std::wstring name = it->path().filename().wstring();
                std::transform(name.begin(), name.end(), name.begin(), ::towlower);
                if (name.find(low) != std::wstring::npos) hits.push_back(it->path());
            }
            int removed = 0;
            for (auto& p : hits) { if (SafeRemoveAll(p) > 0 || SafeRemove(p)) removed++; }
            return removed;
        }

        inline void RemoveKrisp(const fs::path& appDir) {
            int n = PurgeByName(appDir / L"modules", L"krisp");
            if (n) SafeAddLog("DISCORD: Removed Krisp noise-suppression module.");
        }
        inline void RemoveGameSDK(const fs::path& appDir) {
            int n = PurgeByName(appDir, L"discord_game_sdk");
            if (n) SafeAddLog("DISCORD: Removed Game SDK DLL(s).");
        }
        inline void RemoveGamePresence(const fs::path& appDir) {
            int n = PurgeByName(appDir / L"modules", L"discord_rpc");
            n += PurgeByName(appDir / L"modules", L"game_utils");
            if (n) SafeAddLog("DISCORD: Removed Game Presence / RPC module(s).");
        }

        // THE FIX (A4-8): this used to delete libEGL.dll and libGLESv2.dll - which
        // ARE Chromium's ANGLE renderer, not junk - alongside the SwiftShader
        // software fallback. With hardware acceleration also turned off by
        // WriteOptimizedSettings (both default to on, and both are one click apart
        // on the same page), that removes the GPU path and its fallback at the same
        // time. "Remove junk files" is not a licence to take away every way the app
        // has of drawing itself.
        //
        // Vulkan, SwiftShader and the .sig files are genuinely unused by Discord and
        // still go. ANGLE and d3dcompiler stay.
        // THE FIX (A4-19): vk_swiftshader_icd.json is the Vulkan ICD manifest that
        // REGISTERS vk_swiftshader.dll - it ships alongside the DLL and contains
        // {"ICD": {"library_path": ".\\vk_swiftshader.dll"}}. Deleting the DLL and
        // the swiftshader\ directory while leaving the manifest behind left a driver
        // registration naming a file that no longer exists. Confirmed by execution:
        // after a clean debloat the manifest was still there, still pointing at the
        // deleted DLL. It goes with what it describes.
        inline void RemoveJunk(const fs::path& appDir) {
            const wchar_t* junkFiles[] = { L"vk_swiftshader.dll", L"vulkan-1.dll", L"vk_swiftshader_icd.json" };
            int removed = 0;
            for (auto d : junkFiles) if (SafeRemove(appDir / d)) removed++;
            if (SafeRemoveAll(appDir / L"swiftshader") > 0) removed++;
            // .sig signature files anywhere under the app dir
            std::error_code ec;
            std::vector<fs::path> sigs;
            for (auto it = fs::recursive_directory_iterator(appDir, fs::directory_options::skip_permission_denied, ec);
                it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it->path().extension() == L".sig") sigs.push_back(it->path());
            }
            for (auto& s : sigs) if (SafeRemove(s)) removed++;
            if (removed) SafeAddLog("DISCORD: Removed junk files (SwiftShader, Vulkan, .sig signatures). ANGLE and d3dcompiler kept - Discord needs them to render.");
        }

        inline void RemoveAutoUpdater() {
            fs::path root = LocalDir();
            int removed = 0;
            if (SafeRemove(root / L"Update.exe")) removed++;
            if (SafeRemove(root / L"RELEASES")) removed++;
            if (SafeRemoveAll(root / L"packages") > 0) removed++;
            if (removed) SafeAddLog("DISCORD: Removed auto-updater (Update.exe / RELEASES / packages).");
        }

        // Discord's autostart is a Run value pointing at Update.exe. Deleting it is a
        // one-way door unless the value is kept, and the Run value is a REG_SZ path, so
        // StateCache's DWORD store cannot hold it - hence this small string cache,
        // parked next to StateCache's own key rather than somewhere new.
        inline const wchar_t* kDiscordCachePath = L"Software\\OptiMaxPro\\DiscordCache";
        inline const wchar_t* kRunPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

        // Copies the current Run value into the cache. Returns false when there was
        // nothing to copy, which is the normal case for the HKLM half.
        inline bool CaptureRunValue(HKEY root, const wchar_t* backupName) {
            HKEY hRun;
            if (RegOpenKeyExW(root, kRunPath, 0, KEY_QUERY_VALUE, &hRun) != ERROR_SUCCESS) return false;
            BYTE data[1024]; DWORD len = sizeof(data), type = 0;
            LSTATUS s = RegQueryValueExW(hRun, L"Discord", NULL, &type, data, &len);
            RegCloseKey(hRun);
            if (s != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;

            HKEY hCache;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, kDiscordCachePath, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hCache, NULL) != ERROR_SUCCESS) return false;
            LONG w = RegSetValueExW(hCache, backupName, 0, type, data, len);
            RegCloseKey(hCache);
            return w == ERROR_SUCCESS;
        }

        // Writes a cached Run value back. The cache entry is only dropped once the
        // write has actually landed, so a refused restore can be retried instead of
        // losing the value it was holding.
        inline bool ReplayRunValue(HKEY root, const wchar_t* backupName) {
            HKEY hCache;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kDiscordCachePath, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hCache) != ERROR_SUCCESS) return false;
            BYTE data[1024]; DWORD len = sizeof(data), type = 0;
            if (RegQueryValueExW(hCache, backupName, NULL, &type, data, &len) != ERROR_SUCCESS) { RegCloseKey(hCache); return false; }

            HKEY hRun;
            bool ok = false;
            if (RegCreateKeyExW(root, kRunPath, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hRun, NULL) == ERROR_SUCCESS) {
                ok = (RegSetValueExW(hRun, L"Discord", 0, type, data, len) == ERROR_SUCCESS);
                RegCloseKey(hRun);
            }
            if (ok) RegDeleteValueW(hCache, backupName);
            RegCloseKey(hCache);
            return ok;
        }

        // THE FIX: this deleted the Run value and kept no copy, so nothing in the
        // program - not this page, not "Restore All Defaults" - could ever put Discord
        // back in the user's startup. That is the dead-revert-path shape RestoreFSO()
        // was written for, in the same feature, one option away.
        inline void DisableAutostart() {
            bool cleared = false;
            HKEY hKey;
            CaptureRunValue(HKEY_CURRENT_USER, L"RunHKCU");
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunPath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                if (RegDeleteValueW(hKey, L"Discord") == ERROR_SUCCESS) cleared = true;
                RegCloseKey(hKey);
            }
            CaptureRunValue(HKEY_LOCAL_MACHINE, L"RunHKLM");
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRunPath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                if (RegDeleteValueW(hKey, L"Discord") == ERROR_SUCCESS) cleared = true;
                RegCloseKey(hKey);
            }
            SafeAddLog(cleared ? "DISCORD: Autostart Run key removed - the original is saved and can be put back with Restore All Defaults."
                               : "DISCORD: No Discord autostart Run key was present.");
        }

        // Undoes DisableAutostart(). Silent when there is nothing cached, because
        // "Restore All Defaults" calls this on every machine, including those that
        // never ran the Discord debloat.
        inline int RestoreAutostart() {
            int restored = 0;
            if (ReplayRunValue(HKEY_CURRENT_USER, L"RunHKCU")) restored++;
            if (ReplayRunValue(HKEY_LOCAL_MACHINE, L"RunHKLM")) restored++;
            if (restored) SafeAddLog("DISCORD: Autostart Run entry restored - Discord will start with Windows again.");
            return restored;
        }

        // THE FIX (A4-2): RegSetValueExW's result was discarded and the success line
        // logged whenever the key merely OPENED, so a refused write still reported
        // that Fullscreen Optimizations had been disabled.
        inline bool DisableFSO(const fs::path& appDir) {
            fs::path exe = appDir / L"Discord.exe";
            std::error_code ec;
            if (!fs::exists(exe, ec)) {
                SafeAddLog("DISCORD: Discord.exe not found, so Fullscreen Optimizations were left unchanged.");
                return false;
            }
            HKEY hKey;
            const wchar_t* layers = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
            if (RegCreateKeyExW(HKEY_CURRENT_USER, layers, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
                SafeAddLog("DISCORD WARNING: Could not open the compatibility settings key - Fullscreen Optimizations are unchanged.");
                return false;
            }
            std::wstring data = L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE";
            std::wstring name = exe.wstring();
            bool ok = (RegSetValueExW(hKey, name.c_str(), 0, REG_SZ,
                (const BYTE*)data.c_str(), (DWORD)((data.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
            RegCloseKey(hKey);

            SafeAddLog(ok ? "DISCORD: Fullscreen Optimizations disabled for Discord.exe."
                          : "DISCORD WARNING: Could not write the compatibility setting - Fullscreen Optimizations are unchanged.");
            return ok;
        }

        // Undoes DisableFSO(). Returns how many entries were removed.
        //
        // THE FIX: DisableFSO() was the only writer of an AppCompatFlags\Layers value
        // anywhere in the project, and nothing ever deleted it - not the Discord page,
        // not RestoreAllToWindowsDefaults(). The override outlived uninstalling OptiMax
        // entirely, which is the same dead-revert-path shape as the six V3 tweaks.
        //
        // The value NAME is the full path of whichever Discord.exe was current when the
        // tweak ran, so after a Discord update the old entry is orphaned under a stale
        // app-x.x.x folder. Recomputing the path from LatestAppDir() would miss exactly
        // those, so this enumerates the key instead and removes every entry that points
        // at a Discord.exe AND whose data is precisely the layer we write. A user's own
        // compatibility settings for Discord have different data and are left alone.
        inline int RestoreFSO() {
            const wchar_t* layers = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, layers, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return 0;

            const std::wstring ours = L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE";
            const std::wstring leaf = L"\\discord.exe";

            // A registry value name maxes out at 16383 chars; heap rather than stack
            // because that is 32 KB and this runs on the UI thread.
            std::vector<wchar_t> nameBuf(16384);
            std::vector<std::wstring> doomed;

            for (DWORD i = 0; ; ++i) {
                DWORD nameLen = (DWORD)nameBuf.size();
                BYTE data[512];
                DWORD dataLen = sizeof(data);
                DWORD type = 0;
                LSTATUS s = RegEnumValueW(hKey, i, nameBuf.data(), &nameLen, NULL, &type, data, &dataLen);
                if (s == ERROR_NO_MORE_ITEMS) break;
                // ERROR_MORE_DATA here means a layer string longer than anything we
                // write, so it cannot be ours - skip it rather than abandoning the scan.
                if (s != ERROR_SUCCESS || type != REG_SZ) continue;

                std::wstring vname(nameBuf.data(), nameLen);
                std::wstring low = vname;
                std::transform(low.begin(), low.end(), low.begin(), ::towlower);
                if (low.size() < leaf.size()) continue;
                if (low.compare(low.size() - leaf.size(), leaf.size(), leaf) != 0) continue;

                // Registry strings are not guaranteed to be NUL-terminated.
                std::wstring vdata((const wchar_t*)data, dataLen / sizeof(wchar_t));
                while (!vdata.empty() && vdata.back() == L'\0') vdata.pop_back();
                if (vdata != ours) continue;

                doomed.push_back(vname);
            }

            // Deleted after the walk, not during it - removing a value mid-enumeration
            // shifts the indices and silently skips entries.
            int removed = 0;
            for (auto& d : doomed) if (RegDeleteValueW(hKey, d.c_str()) == ERROR_SUCCESS) removed++;
            RegCloseKey(hKey);

            if (removed) SafeAddLog("DISCORD: Fullscreen Optimizations override removed for Discord.exe - back to the Windows default.");
            return removed;
        }

        // Returns true only if a complete backup now exists on the Desktop.
        //
        // THE FIX (1 of 2): DesktopDir() returns an EMPTY path when SHGetFolderPathW
        // fails, and this built `dst` as `DesktopDir() / L"Discord_Backup"` without
        // checking. An empty path composed with a leaf yields the RELATIVE path
        // "Discord_Backup", which resolves against the current working directory -
        // and this app self-elevates and is started by a logon task, so that
        // directory is C:\Windows\system32. That turned a recursive delete and a
        // whole-folder copy loose in system32, running as administrator, while the
        // log cheerfully reported the backup had gone to the Desktop.
        // CreateDebloatedShortcut() already guards for exactly this; this one did not.
        inline bool BackupInstall() {
            fs::path desktop = DesktopDir();
            if (desktop.empty()) {
                SafeAddLog("DISCORD WARNING: Could not locate your Desktop folder, so no backup was made.");
                return false;
            }

            fs::path src = LocalDir();
            std::error_code ec;
            if (!fs::exists(src, ec)) {
                SafeAddLog("DISCORD WARNING: No Discord install folder found to back up.");
                return false;
            }

            // THE FIX: this did fs::remove_all on any existing Desktop\Discord_Backup
            // and then copied over it. Caught for real during the v4.2.2 GUI pass -
            // the first run backed up the pristine install (1083 items), the second
            // run DELETED that and replaced it with a copy of the already-debloated
            // install (1077 items: no Update.exe, no RELEASES, five language packs
            // left). The one pristine copy - the entire reason this option exists -
            // was destroyed by using the feature twice, and the log said "Full backup
            // copied" both times, which was true and worthless.
            //
            // It also left a window in which NO backup existed at all: between the
            // remove_all and the end of the copy, neither the old nor the new one was
            // on disk, so a failure in between lost both. That window is how this was
            // noticed - a check happened to land inside it.
            //
            // Nothing is deleted now. A repeat backup goes to its own timestamped
            // folder, so the earliest one - the only pristine one - always survives.
            fs::path dst = desktop / L"Discord_Backup";
            if (fs::exists(dst, ec)) {
                SYSTEMTIME st; GetLocalTime(&st);
                wchar_t stamp[40];
                swprintf_s(stamp, L"Discord_Backup_%04d-%02d-%02d_%02d%02d%02d",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                dst = desktop / stamp;
                SafeAddLog("DISCORD: A backup already exists, so this one goes to a new folder - the earlier backup is left untouched.");
            }
            ec.clear();

            // THE FIX: this was one fs::copy(recursive) call, which reports nothing
            // until it returns - so for the length of a 527 MB copy there was no
            // progress to show even if something had been watching. Walking the tree
            // by hand costs a metadata-only pre-pass (a fraction of a second) and is
            // what makes a real percentage possible instead of an indefinite spinner.
            uintmax_t total = 0;
            for (auto it = fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec);
                it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                total++;
            }
            if (total == 0) total = 1;

            ec.clear();
            uintmax_t copied = 0;
            bool anyFailed = false;
            for (auto it = fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec);
                it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) { anyFailed = true; break; }

                std::error_code relEc;
                fs::path rel = fs::relative(it->path(), src, relEc);
                if (relEc) { anyFailed = true; continue; }

                fs::path target = dst / rel;
                std::error_code opEc;
                if (it->is_directory(opEc)) {
                    fs::create_directories(target, opEc);
                }
                else {
                    fs::create_directories(target.parent_path(), opEc);
                    fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, opEc);
                }
                if (opEc) anyFailed = true;

                copied++;
                // Every 25 entries, not every entry: the status write takes a mutex
                // and this loop runs thousands of times.
                if ((copied % 25) == 0 || copied == total) {
                    Progress::Set("Backing up Discord - " + std::to_string(copied) + " of " + std::to_string(total) + " items",
                        (int)((copied * 100) / total));
                }
            }

            if (!anyFailed) {
                // Names the folder it actually wrote. "Desktop\Discord_Backup" was
                // hardcoded, so once a second backup went somewhere else the line
                // would have pointed the user at the wrong folder.
                SafeAddLog("DISCORD: Full backup copied to Desktop\\" + dst.filename().string()
                    + " (" + std::to_string((unsigned long long)copied) + " items).");
                return true;
            }
            SafeAddLog("DISCORD WARNING: Backup could not be completed (some files may be in use). Nothing has been removed.");
            return false;
        }

        // ----- orchestrator ---------------------------------------------------
        // Returns false (and logs why) if it refuses to run. The caller should
        // surface that to the user.
        inline bool RunDebloat(const DebloatOptions& o) {
            if (!IsInstalled()) { SafeAddLog("DISCORD: No Discord installation found - nothing to debloat."); return false; }
            if (IsRunning()) { SafeAddLog("DISCORD: Discord is running. Close it fully (including the tray icon) and try again."); return false; }

            // THE FIX (2 of 2): the backup's result was discarded and every deletion
            // below ran regardless. A user who ticks "Create backup to Desktop first"
            // is asking for a way back before anything is destroyed - so a backup that
            // did not happen has to stop the operation, not be logged past. Deleting
            // their Discord install after failing to make the copy they asked for is
            // the one outcome this option exists to prevent.
            if (o.backup) {
                Progress::Set("Backing up Discord...", 0);
                if (!BackupInstall()) {
                    SafeAddLog("DISCORD: Stopping - the backup you asked for could not be made, so nothing has been removed.");
                    return false;
                }
            }

            // Past the backup the remaining steps are quick, so they report a stage
            // name rather than a percentage - a bar that jumps 0 to 100 in a second
            // is worse than no bar.
            Progress::Set("Removing old versions...");
            fs::path appDir = LatestAppDir();
            RemoveOldVersions(appDir);

            if (!appDir.empty()) {
                Progress::Set("Removing unused language packs...");
                RemoveLanguagePacks(appDir, o.keepEnglish, o.keepFrench);
                if (o.removeKrisp) { Progress::Set("Removing Krisp..."); RemoveKrisp(appDir); }
                if (o.removeGameSDK) { Progress::Set("Removing Game SDK..."); RemoveGameSDK(appDir); }
                if (o.removeRPC) { Progress::Set("Removing Game Presence..."); RemoveGamePresence(appDir); }
                if (o.removeJunk) { Progress::Set("Removing junk files..."); RemoveJunk(appDir); }
                if (o.disableFSO) { Progress::Set("Disabling Fullscreen Optimizations..."); DisableFSO(appDir); }
            }

            if (o.removeUpdater) {
                Progress::Set("Removing the auto-updater...");
                RemoveAutoUpdater();
                if (!appDir.empty()) CreateDebloatedShortcut(appDir / L"Discord.exe");
            }
            if (o.disableAutostart) { Progress::Set("Removing autostart..."); DisableAutostart(); }

            Progress::Set("Finished.", 100);
            SafeAddLog("DISCORD: Debloat complete.");
            return true;
        }

        // ----- optimize settings.json ----------------------------------------
        // Upserts a boolean TOP-LEVEL key while preserving every other setting.
        //
        // THE FIX (A4-7): the "top-level" in that sentence was a claim, not a
        // behaviour. Json::FindKey matches a key at ANY depth by design, so a
        // settings.json with a nested object carrying one of these names would have
        // had the nested copy rewritten while the real top-level setting was left
        // alone - and the log would have said the setting was applied. Discord's
        // settings.json is flat today, which is the only reason this never bit.
        // TopLevelKeyValuePos() below only accepts a match at brace depth 1.
        inline size_t TopLevelKeyValuePos(const std::string& j, const std::string& key) {
            int depth = 0;
            size_t i = 0;
            while (i < j.size()) {
                char c = j[i];
                if (c == '"') {
                    std::string token = OptiMax::Json::Unescape(OptiMax::Json::ReadRawString(j, i));
                    size_t after = i;
                    while (after < j.size() && isspace((unsigned char)j[after])) ++after;
                    if (after < j.size() && j[after] == ':' && depth == 1 && token == key) {
                        return after + 1;   // index just past ':'
                    }
                    continue;               // ReadRawString already advanced i
                }
                if (c == '{' || c == '[') depth++;
                else if (c == '}' || c == ']') depth--;
                ++i;
            }
            return std::string::npos;
        }

        inline void UpsertBool(std::string& j, const std::string& key, bool value) {
            std::string valStr = value ? "true" : "false";
            size_t brace = j.find('{');
            if (brace == std::string::npos) { j = "{\n  \"" + key + "\": " + valStr + "\n}"; return; }

            size_t kp = TopLevelKeyValuePos(j, key); // index just past ':'
            if (kp != std::string::npos) {
                size_t vs = kp;
                while (vs < j.size() && isspace((unsigned char)j[vs])) vs++;
                size_t ve = vs; int depth = 0;
                while (ve < j.size()) {
                    char c = j[ve];
                    if (c == '"') { OptiMax::Json::ReadRawString(j, ve); continue; }
                    if (c == '{' || c == '[') depth++;
                    else if (c == '}' || c == ']') { if (depth == 0) break; depth--; }
                    else if (c == ',' && depth == 0) break;
                    ve++;
                }
                j.replace(vs, ve - vs, valStr);
            }
            else {
                size_t after = brace + 1;
                while (after < j.size() && isspace((unsigned char)j[after])) after++;
                bool empty = (after < j.size() && j[after] == '}');
                std::string ins = "\n  \"" + key + "\": " + valStr;
                if (!empty) ins += ",";
                j.insert(brace + 1, ins);
            }
        }

        inline bool WriteOptimizedSettings() {
            fs::path dir = RoamingDir();
            std::error_code ec;
            if (!fs::exists(dir, ec)) { SafeAddLog("DISCORD: Roaming folder not found - is Discord installed?"); return false; }
            fs::path file = dir / L"settings.json";

            std::string content;
            { std::ifstream in(file, std::ios::binary); if (in) { std::stringstream ss; ss << in.rdbuf(); content = ss.str(); } }
            if (content.find('{') == std::string::npos) content = "{}";

            UpsertBool(content, "enableHardwareAcceleration", false);
            UpsertBool(content, "SKIP_HOST_UPDATE", true);
            UpsertBool(content, "DEVELOPER_MODE", true);
            UpsertBool(content, "MINIMIZE_TO_TRAY", true);
            UpsertBool(content, "debugLogging", false);
            UpsertBool(content, "IS_MAXIMIZED", true);
            UpsertBool(content, "START_MINIMIZED", false);
            // Disable in-app debugging: turn off the DevTools console and verbose
            // logging. (debugLogging above already covers the log side.) These are the
            // only debugging switches that live in settings.json - everything else
            // Discord exposes is an account-synced setting the server owns.
            UpsertBool(content, "DANGEROUS_ENABLE_DEVTOOLS_ONLY_ENABLE_IF_YOU_KNOW_WHAT_YOURE_DOING", false);

            // THE FIX: this opened settings.json with ios::trunc - destroying the
            // user's existing settings - and then never checked that the write
            // succeeded, logging success either way. A failure part-way through (disk
            // full, antivirus lock, Discord starting up underneath us) left a
            // truncated settings.json behind while reporting the settings had been
            // applied. Written to a temp file first and moved into place only once it
            // is complete, so the original survives any failure.
            fs::path tmp = file;
            tmp += L".optimax.tmp";
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out) { SafeAddLog("DISCORD: Could not write settings.json (is Discord running?)."); return false; }
                out << content;
                out.flush();
                if (!out) {
                    out.close();
                    std::error_code rmEc;
                    fs::remove(tmp, rmEc);
                    SafeAddLog("DISCORD: Could not write settings.json - your existing settings are unchanged.");
                    return false;
                }
            }

            if (!MoveFileExW(tmp.wstring().c_str(), file.wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) {
                std::error_code rmEc;
                fs::remove(tmp, rmEc);
                SafeAddLog("DISCORD: Could not replace settings.json (is Discord running?) - your existing settings are unchanged.");
                return false;
            }

            SafeAddLog("DISCORD: Performance settings.json applied (hardware acceleration off, update checks skipped, tray on close, in-app DevTools/debugging off).");
            return true;
        }

        // ----- clean cache ----------------------------------------------------
        inline void CleanCache(bool removeLocalStorage) {
            fs::path dir = RoamingDir();
            std::error_code ec;
            if (!fs::exists(dir, ec)) { SafeAddLog("DISCORD: Roaming folder not found - nothing to clean."); return; }

            const wchar_t* targets[] = {
                L"Cache", L"Code Cache", L"GPUCache", L"ShaderCache", L"VideoDecodeStats",
                L"Cookies", L"Web Data", L"Databases", L"Session Storage", L"logs",
                L"Crashpad", L"debug", L"sentry", L"WidevineCdm", L"MediaFoundationWidevineCdm",
                L"blob_storage", L"CacheStorage", L"shared_proto_db", L"DawnCache", L"GrShaderCache",
                L"component_crx_cache", L"extensions_crx_cache"
            };
            uintmax_t freedItems = 0;
            for (auto t : targets) {
                uintmax_t n = SafeRemoveAll(dir / t);
                if (n == 0) { if (SafeRemove(dir / t)) freedItems++; }
                else freedItems += n;
            }
            if (removeLocalStorage) {
                SafeRemoveAll(dir / L"Local Storage");
                SafeAddLog("DISCORD: Local Storage cleared - you will be signed out on next launch.");
            }
            SafeAddLog("DISCORD: Cache cleaned (" + std::to_string((unsigned long long)freedItems) + " items removed).");
        }

    } // namespace Discord
} // namespace OptiMax
