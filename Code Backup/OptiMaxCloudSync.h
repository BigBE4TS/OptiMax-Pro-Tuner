#pragma once

// --- CRITICAL FIX FOR THE 180 NETWORK ERRORS ---
// Same ordering rule the rest of this project follows: the modern network
// library has to be reachable before <windows.h> pulls in winsock 1.1.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wininet.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>

#include "OptiMaxGlobals.h"

#pragma comment(lib, "wininet.lib")

// =========================================================================
// COMMUNITY PROFILE DATABASE (CLOUD SYNC)
//
// DESIGN CONSTRAINT - DO NOT CHANGE THIS:
// This database is fetched and DISPLAYED ONLY. Nothing in this file, and
// nothing that consumes it, may auto-apply a setting or auto-add an entry to
// the user's game library. A cloud profile only takes effect after the user
// explicitly clicks to copy it into their own local library (see the Community
// Profiles tab in OptiMaxApp.h).
//
// The reason is deliberate: profiles.db is a remotely-editable file. If a line
// in it could automatically cause action against an arbitrary matching process
// on a user's machine, then anyone who can edit that file - or anyone who can
// interpose on the fetch - effectively gets to reconfigure every install. That
// is not an acceptable shape for this feature regardless of what the file
// happens to contain today. The opt-in gate is the whole safety model; keep it.
// =========================================================================

namespace OptiMax {
    namespace CloudSync {

        // One line of profiles.db, parsed. Field order is fixed by the format:
        // exeName|disableSMT|highPriority|aggressiveRAMPurge|disableFSO|engineType
        struct GameProfile {
            std::string exeName;
            bool disableSMT = false;
            bool highPriority = false;
            bool aggressiveRAMPurge = false;
            bool disableFSO = false;
            std::string engineType;

            // Section the line appeared under in profiles.db. NOT a seventh
            // field - see ParseDatabase for why that would have been a
            // compatibility disaster. Empty when a line precedes any section.
            std::string category;
        };

        static inline const std::string PROFILE_DB_URL =
            "https://raw.githubusercontent.com/BigBE4TS/OptiMax-Pro-Tuner/main/profiles.db";

        // Hard ceiling on the download. profiles.db is a few KB of plain text;
        // anything remotely near this is either wrong or hostile, and we would
        // rather stop reading than let a remote file dictate how much memory
        // this process allocates.
        static inline const size_t MAX_DB_BYTES = 512 * 1024;

        // Guards against one absurd line eating the whole budget, and keeps the
        // UI table from having to render a 40KB "engine name".
        static inline const size_t MAX_FIELD_CHARS = 64;

        inline std::atomic<bool> g_cloudSyncInProgress{ false };
        inline std::atomic<bool> g_cloudSyncEverSucceeded{ false };
        inline std::atomic<int>  g_cloudProfileCount{ 0 };

        namespace detail {
            inline std::mutex g_cloudMutex;
            inline std::map<std::string, GameProfile> g_cloudProfiles;
            inline std::string g_lastSyncTime = "Never";

            inline std::string Trim(const std::string& in) {
                size_t first = in.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) return "";
                size_t last = in.find_last_not_of(" \t\r\n");
                return in.substr(first, (last - first) + 1);
            }

            // A profile's exe name ends up compared against real running
            // processes and written into the user's library, so it has to look
            // like a bare filename and nothing else. Rejecting path separators
            // here means a malformed or malicious line can never turn into a
            // path anywhere downstream.
            inline bool IsPlausibleExeName(const std::string& name) {
                if (name.empty() || name.size() > MAX_FIELD_CHARS) return false;
                if (name.find_first_of("\\/:*?\"<>|") != std::string::npos) return false;
                for (unsigned char c : name) {
                    if (c < 0x20 || c == 0x7F) return false;
                }
                if (name.size() < 5) return false;
                std::string tail = name.substr(name.size() - 4);
                for (auto& c : tail) c = (char)tolower((unsigned char)c);
                return tail == ".exe";
            }

            // Free-text field (engine name) shown verbatim in the UI table.
            inline std::string SanitizeText(const std::string& in) {
                std::string out;
                for (unsigned char c : in) {
                    if (c >= 0x20 && c != 0x7F) out += (char)c;
                    if (out.size() >= MAX_FIELD_CHARS) break;
                }
                return out;
            }

            inline bool ParseBoolField(const std::string& in) {
                return in == "1" || in == "true" || in == "TRUE";
            }

            inline std::string FormatNow() {
                SYSTEMTIME st;
                GetLocalTime(&st);
                char buf[64];
                sprintf_s(buf, "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
                return std::string(buf);
            }

            // Straight WinINet GET, matching how OptiMaxUpdater.h fetches
            // version.txt. Returns an empty string on any failure - callers
            // must treat that as "keep whatever was already cached".
            inline std::string FetchDatabase() {
                HINTERNET hInternet = InternetOpenA("OptiMax CloudSync", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
                if (!hInternet) return "";

                // THE FIX (1 of 2): no cache buster. INTERNET_FLAG_RELOAD clears only
                // the local WinINet cache, so a shared cache in between could keep
                // serving a stale profiles.db. Identical to the defect found in
                // OptiMaxUpdater.h; both files were written from the same template.
                //
                // ⚠️ A5-4: this helps with intermediate proxies only. It does NOT
                // work on raw.githubusercontent.com, which is where PROFILE_DB_URL
                // points - that CDN keys on path alone and ignores the query
                // string. Measured during the v4.2.0 publish; see the fuller note
                // in OptiMaxUpdater.h.
                // Expect published profile changes to reach clients on a ~5 minute
                // TTL, which is tolerable here: unlike the updater, a stale
                // profiles.db has no second file it has to stay consistent with.
                std::string url = PROFILE_DB_URL;
                url += (url.find('?') == std::string::npos) ? "?t=" : "&t=";
                url += std::to_string(GetTickCount64());

                HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
                    INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
                if (!hConnect) {
                    InternetCloseHandle(hInternet);
                    return "";
                }

                // THE FIX (2 of 2): the HTTP status was never checked, so a 404 body
                // ("404: Not Found") was fed to the parser as though it were profile
                // data. It happened to fail closed - no line has six fields - but the
                // user was then told the database "contained no usable entries", which
                // points at the wrong problem entirely. This is the same mistake that
                // made a missing checksum file undiagnosable in the updater.
                DWORD status = 0;
                DWORD statusLen = sizeof(status);
                if (!HttpQueryInfoA(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                    &status, &statusLen, NULL)) {
                    status = 0;
                }
                if (status != 200) {
                    SafeAddLog("CLOUD SYNC: The community profile database is not reachable (server returned "
                        + std::to_string(status) + ").");
                    InternetCloseHandle(hConnect);
                    InternetCloseHandle(hInternet);
                    return "";
                }

                std::string body;
                char buffer[8192];
                DWORD bytesRead = 0;
                while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                    if (body.size() + bytesRead > MAX_DB_BYTES) {
                        body.append(buffer, MAX_DB_BYTES - body.size());
                        break;
                    }
                    body.append(buffer, bytesRead);
                }

                InternetCloseHandle(hConnect);
                InternetCloseHandle(hInternet);
                return body;
            }

            // Skips blank lines, '#' comments, and any line that doesn't have
            // exactly the six expected fields or whose exe name doesn't look
            // like a filename. One bad line never invalidates the rest of the
            // file, and never produces a half-populated GameProfile.
            inline std::map<std::string, GameProfile> ParseDatabase(const std::string& body, int& skippedOut) {
                std::map<std::string, GameProfile> parsed;
                skippedOut = 0;

                // Category of the section currently being read. Carried down
                // onto each profile below it.
                std::string currentCategory;

                size_t start = 0;
                while (start <= body.size()) {
                    size_t end = body.find('\n', start);
                    std::string line = (end == std::string::npos)
                        ? body.substr(start)
                        : body.substr(start, end - start);

                    if (end == std::string::npos) start = body.size() + 1;
                    else start = end + 1;

                    line = Trim(line);
                    if (line.empty()) continue;

                    if (line[0] == '#') {
                        // A section header doubles as a category marker:
                        //     # --- Competitive FPS ---
                        //
                        // Deliberately carried as a COMMENT rather than a
                        // seventh field. The parser below requires EXACTLY six
                        // fields, so adding a seventh would make every client
                        // that has already shipped skip every single line of
                        // the file - the feature would break for everyone the
                        // moment the new database went live. Older clients
                        // ignore '#' lines outright, so grouping costs them
                        // nothing and they keep working unchanged.
                        std::string c = Trim(line.substr(1));
                        if (c.size() > 6 && c.rfind("---", 0) == 0
                            && c.compare(c.size() - 3, 3, "---") == 0) {
                            currentCategory = SanitizeText(Trim(c.substr(3, c.size() - 6)));
                        }
                        continue;
                    }

                    std::vector<std::string> fields;
                    size_t fieldStart = 0;
                    while (true) {
                        size_t bar = line.find('|', fieldStart);
                        if (bar == std::string::npos) {
                            fields.push_back(Trim(line.substr(fieldStart)));
                            break;
                        }
                        fields.push_back(Trim(line.substr(fieldStart, bar - fieldStart)));
                        fieldStart = bar + 1;
                        if (fields.size() > 8) break; // malformed, bail out early
                    }

                    if (fields.size() != 6) { skippedOut++; continue; }
                    if (!IsPlausibleExeName(fields[0])) { skippedOut++; continue; }

                    GameProfile p;
                    p.exeName = fields[0];
                    p.disableSMT = ParseBoolField(fields[1]);
                    p.highPriority = ParseBoolField(fields[2]);
                    p.aggressiveRAMPurge = ParseBoolField(fields[3]);
                    p.disableFSO = ParseBoolField(fields[4]);
                    p.engineType = SanitizeText(fields[5]);
                    p.category = currentCategory;

                    parsed[p.exeName] = p;
                }

                return parsed;
            }
        } // namespace detail

        // Returns a copy so the UI can iterate it without holding the mutex
        // across a whole frame's worth of ImGui calls.
        inline std::map<std::string, GameProfile> GetSnapshot() {
            std::lock_guard<std::mutex> lock(detail::g_cloudMutex);
            return detail::g_cloudProfiles;
        }

        inline std::string GetLastSyncTime() {
            std::lock_guard<std::mutex> lock(detail::g_cloudMutex);
            return detail::g_lastSyncTime;
        }

        // Fire-and-forget. Self-detaches, so it's safe to call from app init or
        // straight off a button press without blocking the UI thread. Calling
        // it while a sync is already running is a no-op rather than a second
        // concurrent fetch.
        inline void StartSync() {
            bool expected = false;
            if (!g_cloudSyncInProgress.compare_exchange_strong(expected, true)) return;

            std::thread([]() {
                // An exception escaping a std::thread's entry point calls
                // std::terminate and takes the whole application down - there is no
                // stack to unwind into. Nothing here is expected to throw, but a
                // background refresh of an optional, display-only list is never worth
                // that risk, and without this the in-progress flag would also stay
                // stuck true and block every later sync.
                try {
                std::string body = detail::FetchDatabase();

                if (body.empty()) {
                    SafeAddLog("CLOUD SYNC: Could not reach the community profile database. Keeping the cached copy.");
                    g_cloudSyncInProgress = false;
                    return;
                }

                int skipped = 0;
                std::map<std::string, GameProfile> parsed = detail::ParseDatabase(body, skipped);

                if (parsed.empty()) {
                    SafeAddLog("CLOUD SYNC: Downloaded the profile database but found no usable entries. Keeping the cached copy.");
                    g_cloudSyncInProgress = false;
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(detail::g_cloudMutex);
                    detail::g_cloudProfiles = parsed;
                    detail::g_lastSyncTime = detail::FormatNow();
                }

                g_cloudProfileCount = (int)parsed.size();
                g_cloudSyncEverSucceeded = true;

                std::string msg = "CLOUD SYNC: Loaded " + std::to_string(parsed.size()) +
                    " community profile(s). Nothing is applied until you add one to your library.";
                if (skipped > 0) msg += " (" + std::to_string(skipped) + " malformed line(s) ignored.)";
                SafeAddLog(msg);

                g_cloudSyncInProgress = false;
                }
                catch (...) {
                    SafeAddLog("CLOUD SYNC: Stopped unexpectedly while refreshing community profiles. "
                        "The cached list is unchanged.");
                    g_cloudSyncInProgress = false;
                }
                }).detach();
        }

    } // namespace CloudSync
} // namespace OptiMax
