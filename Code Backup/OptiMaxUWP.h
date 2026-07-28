#pragma once
// C++/WinRT's blocking .get() uses <experimental/coroutine> under C++17, which
// this MSVC toolset now guards behind a hard error. Silence just that one
// deprecation so the PackageManager async calls compile; the codebase is C++17.
// Must be defined before any winrt/ header pulls the coroutine support in.
#ifndef _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>

// C++/WinRT is used ONLY here, to reach the native PackageManager. This removes
// Store apps without spawning PowerShell (Get-AppxPackage | Remove-AppxPackage),
// which is the whole point - a spawned PowerShell uninstall is a strong AV
// heuristic, and the entire debloat effort is about NOT looking like that.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Management.Deployment.h>

#pragma comment(lib, "windowsapp.lib")

extern void SafeAddLog(const std::string& msg);

namespace OptiMax {
    namespace UWP {

        struct AppEntry {
            std::wstring name;      // package family Name, e.g. Microsoft.XboxGamingOverlay
            std::wstring fullName;  // PackageFullName - what RemovePackage needs
            std::string  display;   // friendly label for the UI
            bool selected = false;  // checkbox state
        };

        // Curated bloat catalog. {package Name, friendly label, default-checked}.
        // Only entries actually installed on the machine are shown, so this list can
        // safely be broad without cluttering the UI.
        struct CatalogItem { const wchar_t* name; const char* display; bool defaultSel; };

        inline const std::vector<CatalogItem>& Catalog() {
            static const std::vector<CatalogItem> c = {
                { L"Microsoft.XboxGamingOverlay",       "Xbox Game Bar",              true  },
                { L"Microsoft.XboxGameOverlay",         "Xbox Game Overlay",         true  },
                { L"Microsoft.Xbox.TCUI",               "Xbox Live UI",              false },
                { L"Microsoft.XboxIdentityProvider",    "Xbox Identity Provider",    false },
                { L"Microsoft.XboxSpeechToTextOverlay", "Xbox Speech-To-Text",       true  },
                { L"Microsoft.GamingApp",               "Xbox App",                  false },
                { L"Microsoft.BingNews",                "Microsoft News",            true  },
                { L"Microsoft.BingWeather",             "Weather",                   true  },
                { L"Microsoft.BingSearch",              "Web Search (Bing)",         true  },
                { L"Microsoft.WindowsFeedbackHub",      "Feedback Hub",              true  },
                { L"Microsoft.MicrosoftSolitaireCollection", "Solitaire Collection", true },
                { L"Microsoft.ZuneMusic",               "Media Player / Groove",     false },
                { L"Microsoft.ZuneVideo",               "Movies & TV",               false },
                { L"Microsoft.Clipchamp",               "Clipchamp",                 true  },
                { L"Microsoft.People",                  "People",                    true  },
                { L"microsoft.windowscommunicationsapps","Mail & Calendar",          false },
                { L"Microsoft.YourPhone",               "Phone Link",                false },
                { L"Microsoft.GetHelp",                 "Get Help",                  true  },
                { L"Microsoft.Getstarted",              "Tips",                      true  },
                { L"Microsoft.MicrosoftOfficeHub",      "Office Hub",                true  },
                { L"Microsoft.Todos",                   "Microsoft To Do",           false },
                { L"Microsoft.PowerAutomateDesktop",    "Power Automate",            true  },
                { L"Microsoft.WindowsMaps",             "Maps",                      true  },
                { L"MSTeams",                           "Teams (personal)",          true  },
                { L"MicrosoftTeams",                    "Teams (consumer)",          true  },
                { L"Microsoft.OutlookForWindows",       "New Outlook",               false },
                { L"Microsoft.549981C3F5F10",           "Cortana",                   true  },
                { L"Microsoft.SkypeApp",                "Skype",                     true  },
                { L"Microsoft.MicrosoftStickyNotes",    "Sticky Notes",              false },
                { L"Microsoft.WindowsAlarms",           "Alarms & Clock",            false },
                { L"Microsoft.WindowsSoundRecorder",    "Sound Recorder",            false },
            };
            return c;
        }

        // Enumerates the current user's installed packages and returns the catalog
        // entries that are actually present (with their real PackageFullName). Runs
        // on a background thread - it creates its own MTA apartment so it never
        // collides with the UI thread's COM/WIC apartment.
        inline std::vector<AppEntry> ScanInstalled() {
            std::vector<AppEntry> found;
            bool inited = false;
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                inited = true;
            }
            catch (...) { /* already initialised on this thread - fine */ }

            try {
                winrt::Windows::Management::Deployment::PackageManager pm;
                // Empty SID = current user.
                auto packages = pm.FindPackagesForUser(L"");

                // Build a quick name -> fullName map of what's installed.
                for (auto const& cat : Catalog()) {
                    for (auto const& pkg : packages) {
                        auto id = pkg.Id();
                        if (_wcsicmp(id.Name().c_str(), cat.name) == 0) {
                            AppEntry e;
                            e.name = id.Name().c_str();
                            e.fullName = id.FullName().c_str();
                            e.display = cat.display;
                            // Nothing is ever pre-ticked. The list used to open with
                            // cat.defaultSel applied, which on a Home/Pro install meant
                            // ~12 apps already selected - Office Hub, both Teams, Cortana,
                            // Skype and Maps among them - inside a scrolling child window,
                            // with no confirmation between "Remove Selected Apps" and the
                            // removal thread. The label directly above this list already
                            // says "tick the ones to remove", so the pre-ticking also
                            // contradicted the UI's own description of itself.
                            // defaultSel is kept as curation data (which entries are
                            // commonly-unwanted) for a possible future "select
                            // recommended" button; it deliberately no longer decides
                            // what is checked on open.
                            e.selected = false;
                            found.push_back(e);
                            break;
                        }
                    }
                }
            }
            catch (winrt::hresult_error const& ex) {
                SafeAddLog("DEBLOAT: Could not enumerate Store apps (" + winrt::to_string(ex.message()) + ").");
            }
            catch (...) {
                SafeAddLog("DEBLOAT: Could not enumerate Store apps.");
            }

            if (inited) winrt::uninit_apartment();
            return found;
        }

        // Removes the given packages (by PackageFullName) for the current user.
        // Best-effort and logged per app; inbox apps that refuse removal just fail.
        inline void RemovePackages(const std::vector<AppEntry>& toRemove) {
            bool inited = false;
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                inited = true;
            }
            catch (...) {}

            int ok = 0, fail = 0;
            try {
                winrt::Windows::Management::Deployment::PackageManager pm;
                for (auto const& e : toRemove) {
                    if (e.fullName.empty()) continue;
                    try {
                        auto op = pm.RemovePackageAsync(
                            winrt::hstring(e.fullName),
                            winrt::Windows::Management::Deployment::RemovalOptions::None);
                        auto result = op.get();  // block until done (we're on a worker thread)

                        // THE FIX: success used to be judged by IsRegistered(), which
                        // reports whether a package is registered - not whether THIS
                        // operation achieved anything. Verified by execution: removing
                        // Xbox Game Bar took the package from installed to gone, and
                        // this branch still logged "could not be removed", then
                        // summarised the run as "0 removed, 1 skipped/failed". Every
                        // successful debloat was reported to the user as a failure.
                        //
                        // ExtendedErrorCode is the documented result of the deployment
                        // operation itself, and is S_OK exactly when the removal worked.
                        winrt::hresult hr = result.ExtendedErrorCode();
                        if (hr != winrt::hresult(S_OK)) {
                            fail++;
                            SafeAddLog("DEBLOAT: '" + e.display + "' could not be removed.");
                        }
                        else {
                            ok++;
                            SafeAddLog("DEBLOAT: Removed '" + e.display + "'.");
                        }
                    }
                    catch (...) {
                        fail++;
                        SafeAddLog("DEBLOAT: '" + e.display + "' could not be removed (it may be a protected inbox app).");
                    }
                }
            }
            catch (...) {}

            SafeAddLog("DEBLOAT: Store-app removal finished (" + std::to_string(ok) + " removed, " + std::to_string(fail) + " skipped/failed).");
            if (inited) winrt::uninit_apartment();
        }

    } // namespace UWP
} // namespace OptiMax
