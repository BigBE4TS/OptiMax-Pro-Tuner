#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>

#include "OptiMaxGlobals.h"
#include "OptiMaxJson.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

// =========================================================================
// USER ACCOUNTS (SUPABASE AUTH)
//
// SECURITY GROUND RULES - these are load-bearing, do not relax them:
//
// 1. ONLY the publishable key appears in this file. It is safe to compile into
//    the client *because* every table it can reach is protected by Row Level
//    Security policies scoped to auth.uid(). The key grants no authority of its
//    own; it only identifies the project.
//
// 2. The secret / service-role key must NEVER appear in this repository, in any
//    committed config, or in the compiled binary. It bypasses RLS completely.
//    If a feature appears to need it, that feature belongs in a server-side
//    function, not in this desktop client. Do not "temporarily" hardcode it.
//
// 3. The refresh token is a real credential. It is stored DPAPI-encrypted
//    (CryptProtectData, current-user scope) - never in plaintext - so it is
//    only recoverable by the same Windows user on the same machine. This is the
//    same protection level browsers use for saved passwords.
//
// Networking uses WinHTTP rather than the WinINet used by Updater/CloudSync.
// The task spec allows either. WinHTTP is the better fit here because this code
// needs real control over request headers, request method, and response status
// codes, and it does not share the system/IE proxy and cache state that WinINet
// does. Both are Windows built-ins, so this adds no dependency.
// =========================================================================

namespace OptiMax {
    namespace Auth {

        static inline const std::string SUPABASE_URL =
            "https://zkqsnviltcjkkhhhjnrq.supabase.co";

        // Publishable (anon) key. Public by design - see rule 1 above.
        static inline const std::string SUPABASE_PUBLISHABLE_KEY =
            "sb_publishable_3p6eLl_xHs84AM4LMtWUVQ_HX-HSf7b";

        static inline const char* REG_PATH = "Software\\OptiMaxPro";
        static inline const char* REG_TOKEN_VALUE = "SessionToken";
        static inline const char* REG_REMEMBER_VALUE = "RememberMe";

        enum class State {
            SignedOut = 0,   // showing the sign-in form
            Restoring = 1,   // silent refresh-token exchange in flight
            Working = 2,   // an interactive request is in flight
            SignedIn = 3,
            Guest = 4    // explicitly chose to continue without an account
        };

        inline std::atomic<int>  g_state{ (int)State::SignedOut };
        inline std::atomic<bool> g_busy{ false };
        // Set when the session becomes valid, so the UI thread can react once
        // (resize the window, leave the launch screen) without polling strings.
        inline std::atomic<bool> g_justAuthenticated{ false };
        inline std::atomic<bool> g_avatarChanged{ false };
        // Signup succeeded but the project requires email confirmation, so there
        // is no session yet. The UI consumes this to drop back to the sign-in form,
        // which is where the user needs to be once they've clicked the link.
        inline std::atomic<bool> g_signupNeedsConfirmation{ false };

        namespace detail {
            inline std::mutex g_mutex;
            inline std::string g_accessToken;
            inline std::string g_refreshToken;
            inline std::string g_userId;
            inline std::string g_username;
            inline std::string g_email;
            inline std::string g_avatarPath;   // storage object path, e.g. "<uid>/avatar.png"
            inline std::string g_lastError;
            // Informational messages - "check your inbox", "pick a username" - kept
            // in their own channel because the UI paints g_lastError red. Telling
            // someone their account was created successfully in alarm-red is a lie
            // about what just happened.
            inline std::string g_lastNotice;
            // Username chosen at signup. Held here because the profile row often
            // can't be written until a later sign-in (see EnsureProfile).
            inline std::string g_pendingUsername;
            inline std::vector<unsigned char> g_avatarBytes;

            inline void SetError(const std::string& msg) {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_lastError = msg;
                g_lastNotice.clear();
            }

            inline void SetNotice(const std::string& msg) {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_lastNotice = msg;
                g_lastError.clear();
            }

            // ---------------- WinHTTP ----------------

            struct Response {
                bool  transportOk = false; // request completed (any HTTP status)
                DWORD status = 0;
                std::string body;
                std::vector<unsigned char> raw;
            };

            inline std::wstring Widen(const std::string& s) {
                if (s.empty()) return L"";
                int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
                std::wstring w(n, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
                return w;
            }

            // Deliberately hand-rolled instead of using WinHttpCrackUrl.
            // OptiMaxUpdater.h includes <wininet.h>, which defines its own
            // URL_COMPONENTS (resolving to the ANSI variant in this non-UNICODE
            // build) and collides with WinHTTP's identically-named struct. Rather
            // than fight the include order, parse the URL directly - it's a handful
            // of string operations and removes the conflict entirely.
            struct ParsedUrl {
                std::wstring host;
                std::wstring path;   // includes any query string
                WORD port = 443;
                bool https = true;
                bool ok = false;
            };

            inline ParsedUrl ParseUrl(const std::string& url) {
                ParsedUrl p;

                size_t hostStart;
                if (url.compare(0, 8, "https://") == 0) { p.https = true;  hostStart = 8; }
                else if (url.compare(0, 7, "http://") == 0) { p.https = false; hostStart = 7; }
                else return p;

                size_t pathStart = url.find('/', hostStart);
                std::string hostPort = (pathStart == std::string::npos)
                    ? url.substr(hostStart)
                    : url.substr(hostStart, pathStart - hostStart);
                std::string path = (pathStart == std::string::npos) ? "/" : url.substr(pathStart);

                WORD port = p.https ? (WORD)443 : (WORD)80;
                size_t colon = hostPort.find(':');
                if (colon != std::string::npos) {
                    port = (WORD)atoi(hostPort.c_str() + colon + 1);
                    hostPort = hostPort.substr(0, colon);
                }
                if (hostPort.empty() || port == 0) return p;

                p.host = Widen(hostPort);
                p.path = Widen(path);
                p.port = port;
                p.ok = true;
                return p;
            }

            inline Response Request(const wchar_t* method,
                const std::string& url,
                const std::vector<std::string>& headers,
                const std::string& body,
                bool wantRawBytes = false) {
                Response result;

                ParsedUrl parsed = ParseUrl(url);
                if (!parsed.ok) return result;

                HINTERNET hSession = WinHttpOpen(L"OptiMax Accounts",
                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
                if (!hSession) return result;

                // Keep these tight: this runs on a background thread, but a user
                // staring at a spinner still deserves to be told it failed.
                WinHttpSetTimeouts(hSession, 8000, 8000, 15000, 15000);

                HINTERNET hConnect = WinHttpConnect(hSession, parsed.host.c_str(), parsed.port, 0);
                if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

                DWORD flags = parsed.https ? WINHTTP_FLAG_SECURE : 0;
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, parsed.path.c_str(),
                    nullptr, WINHTTP_NO_REFERER,
                    WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
                if (!hRequest) {
                    WinHttpCloseHandle(hConnect);
                    WinHttpCloseHandle(hSession);
                    return result;
                }

                for (const auto& h : headers) {
                    std::wstring wh = Widen(h);
                    WinHttpAddRequestHeaders(hRequest, wh.c_str(), (DWORD)wh.size(),
                        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
                }

                BOOL sent = WinHttpSendRequest(hRequest,
                    WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                    (DWORD)body.size(), (DWORD)body.size(), 0);

                if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
                    result.transportOk = true;

                    DWORD statusCode = 0, statusLen = sizeof(statusCode);
                    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusLen,
                        WINHTTP_NO_HEADER_INDEX);
                    result.status = statusCode;

                    // 8 MB ceiling: an avatar is the largest thing fetched here, and
                    // a remote server should never get to dictate our allocation.
                    const size_t kMaxBody = 8 * 1024 * 1024;
                    DWORD available = 0;
                    char buf[16384];
                    while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
                        DWORD toRead = (available > sizeof(buf)) ? (DWORD)sizeof(buf) : available;
                        DWORD read = 0;
                        if (!WinHttpReadData(hRequest, buf, toRead, &read) || read == 0) break;

                        if (wantRawBytes) {
                            if (result.raw.size() + read > kMaxBody) break;
                            result.raw.insert(result.raw.end(), buf, buf + read);
                        }
                        else {
                            if (result.body.size() + read > kMaxBody) break;
                            result.body.append(buf, read);
                        }
                    }
                }

                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return result;
            }

            // Turns a Supabase / PostgREST error body into something a person can
            // act on. Raw SQL constraint text and OAuth error codes are not that.
            inline std::string FriendlyError(const Response& r, const std::string& context) {
                if (!r.transportOk) {
                    return "Couldn't reach the server. Check your internet connection and try again.";
                }

                // Supabase is inconsistent about which key carries the human-readable
                // text depending on which service answered (GoTrue vs PostgREST vs
                // Storage), so check all of them.
                std::string code, msg, desc, errName;
                Json::GetString(r.body, "code", code);
                Json::GetString(r.body, "message", msg);
                if (msg.empty()) Json::GetString(r.body, "msg", msg);
                Json::GetString(r.body, "error_description", desc);
                Json::GetString(r.body, "error", errName);

                std::string lower = msg + " " + desc + " " + errName;
                for (auto& c : lower) c = (char)tolower((unsigned char)c);

                // Postgres unique violation - the username or email is taken.
                if (code == "23505" || lower.find("duplicate key") != std::string::npos) {
                    if (lower.find("username") != std::string::npos)
                        return "That username is already taken. Pick a different one.";
                    return "That value is already in use.";
                }

                // The profiles table / policies haven't been created yet. This is
                // by far the most likely first-run failure, so name it precisely
                // instead of showing a bare 404.
                if (code == "42P01" || lower.find("does not exist") != std::string::npos ||
                    lower.find("relation") != std::string::npos) {
                    return "The account database isn't set up on the server yet (missing 'profiles' table).";
                }
                if (r.status == 404 && context == "profile") {
                    return "The account database isn't set up on the server yet (missing 'profiles' table).";
                }
                if (code == "42501" || r.status == 403) {
                    return "The server rejected that request (permission denied).";
                }

                if (lower.find("invalid login") != std::string::npos ||
                    lower.find("invalid_grant") != std::string::npos ||
                    lower.find("invalid credentials") != std::string::npos) {
                    return "Incorrect email or password.";
                }
                if (lower.find("already registered") != std::string::npos ||
                    lower.find("already been registered") != std::string::npos ||
                    lower.find("user already") != std::string::npos) {
                    return "An account with that email already exists. Try signing in instead.";
                }
                if (lower.find("password") != std::string::npos &&
                    (lower.find("least") != std::string::npos || lower.find("weak") != std::string::npos ||
                        lower.find("short") != std::string::npos)) {
                    return "That password is too short - use at least 6 characters.";
                }
                if (lower.find("email") != std::string::npos && lower.find("valid") != std::string::npos) {
                    return "That doesn't look like a valid email address.";
                }
                if (lower.find("confirm") != std::string::npos) {
                    return "Check your email to confirm the account before signing in.";
                }

                if (!msg.empty()) return msg;
                if (!desc.empty()) return desc;
                if (r.status >= 500) return "The server had a problem. Try again in a moment.";
                return "Something went wrong (HTTP " + std::to_string(r.status) + ").";
            }

            // True when a failed request is really "go click the link in your
            // email" rather than "you did something wrong". Same words, very
            // different tone - the caller uses this to pick the message channel.
            inline bool IsEmailConfirmationMessage(const Response& r) {
                std::string code, msg, alt;
                Json::GetString(r.body, "error_code", code);
                Json::GetString(r.body, "message", msg);
                Json::GetString(r.body, "msg", alt);

                std::string lower = code + " " + msg + " " + alt;
                for (auto& c : lower) c = (char)tolower((unsigned char)c);

                return lower.find("not confirmed") != std::string::npos ||
                    lower.find("not_confirmed") != std::string::npos ||
                    lower.find("email not verified") != std::string::npos;
            }

            // ---------------- DPAPI token storage ----------------

            inline bool SaveRefreshToken(const std::string& token) {
                DATA_BLOB in{}, out{};
                in.pbData = (BYTE*)token.data();
                in.cbData = (DWORD)token.size();

                // CRYPTPROTECT_UI_FORBIDDEN: this can run on a background thread
                // during silent restore; it must never try to raise a dialog.
                if (!CryptProtectData(&in, L"OptiMax session token", nullptr, nullptr, nullptr,
                    CRYPTPROTECT_UI_FORBIDDEN, &out)) {
                    return false;
                }

                bool ok = false;
                HKEY hKey;
                if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,
                    KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                    ok = (RegSetValueExA(hKey, REG_TOKEN_VALUE, 0, REG_BINARY,
                        out.pbData, out.cbData) == ERROR_SUCCESS);
                    RegCloseKey(hKey);
                }

                SecureZeroMemory(out.pbData, out.cbData);
                LocalFree(out.pbData);
                return ok;
            }

            inline bool LoadRefreshToken(std::string& tokenOut) {
                HKEY hKey;
                if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
                    return false;

                DWORD type = 0, size = 0;
                if (RegQueryValueExA(hKey, REG_TOKEN_VALUE, NULL, &type, NULL, &size) != ERROR_SUCCESS
                    || type != REG_BINARY || size == 0 || size > 64 * 1024) {
                    RegCloseKey(hKey);
                    return false;
                }

                std::vector<BYTE> encrypted(size);
                if (RegQueryValueExA(hKey, REG_TOKEN_VALUE, NULL, NULL, encrypted.data(), &size) != ERROR_SUCCESS) {
                    RegCloseKey(hKey);
                    return false;
                }
                RegCloseKey(hKey);

                DATA_BLOB in{}, out{};
                in.pbData = encrypted.data();
                in.cbData = (DWORD)encrypted.size();

                if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                    CRYPTPROTECT_UI_FORBIDDEN, &out)) {
                    // Wrong Windows user, different machine, or a corrupt value.
                    return false;
                }

                tokenOut.assign((char*)out.pbData, out.cbData);
                SecureZeroMemory(out.pbData, out.cbData);
                LocalFree(out.pbData);
                return !tokenOut.empty();
            }

            // Returns false only if a stored token is still on disk afterwards.
            //
            // THE FIX: this returned void and checked nothing. It is the local half of
            // signing out, so a failure here leaves a working refresh token in the
            // registry of a machine whose user believes they have signed out - and the
            // app said "Signed out." either way. Of every unchecked write in this
            // project, this is the one with a security meaning.
            inline bool ClearRefreshToken() {
                HKEY hKey;
                LSTATUS open = RegOpenKeyExA(HKEY_CURRENT_USER, REG_PATH, 0, KEY_SET_VALUE, &hKey);
                if (open == ERROR_FILE_NOT_FOUND) return true;   // nothing stored at all
                if (open != ERROR_SUCCESS) return false;

                // Delete the value outright rather than blanking it - a zeroed
                // value still leaves the old ciphertext on disk in the hive's
                // free space, and an empty value is ambiguous with "never set".
                LSTATUS delToken = RegDeleteValueA(hKey, REG_TOKEN_VALUE);
                RegDeleteValueA(hKey, REG_REMEMBER_VALUE);
                RegCloseKey(hKey);

                // Already absent is the state being asked for.
                return delToken == ERROR_SUCCESS || delToken == ERROR_FILE_NOT_FOUND;
            }

            // ---------------- session plumbing ----------------

            inline std::vector<std::string> AuthHeaders(const std::string& accessToken, bool json = true) {
                std::vector<std::string> h;
                h.push_back("apikey: " + SUPABASE_PUBLISHABLE_KEY);
                if (!accessToken.empty()) h.push_back("Authorization: Bearer " + accessToken);
                if (json) h.push_back("Content-Type: application/json");
                return h;
            }

            inline std::string CurrentAccessToken() {
                std::lock_guard<std::mutex> lock(g_mutex);
                return g_accessToken;
            }
            inline std::string CurrentUserId() {
                std::lock_guard<std::mutex> lock(g_mutex);
                return g_userId;
            }

            // Pulls tokens + user id out of a signup/token response.
            inline bool AdoptSession(const std::string& body, std::string& userIdOut) {
                std::string access, refresh, userId;
                Json::GetString(body, "access_token", access);
                Json::GetString(body, "refresh_token", refresh);

                // Scope the "id" lookup to the nested user object - a bare search
                // for "id" could otherwise match something else in the payload.
                std::string userObj;
                if (Json::GetRawValue(body, "user", userObj)) {
                    Json::GetString(userObj, "id", userId);
                }

                if (access.empty() || userId.empty()) return false;

                std::string email;
                if (!userObj.empty()) Json::GetString(userObj, "email", email);

                // The username the account was created with rides along in the
                // auth user's metadata, which is the only place it survives when
                // signup can't write the profile row itself (email confirmation).
                std::string metaName, metaObj;
                if (!userObj.empty() && Json::GetRawValue(userObj, "user_metadata", metaObj)) {
                    Json::GetString(metaObj, "username", metaName);
                }

                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_accessToken = access;
                    if (!refresh.empty()) g_refreshToken = refresh;
                    g_userId = userId;
                    if (!email.empty()) g_email = email;
                    if (!metaName.empty()) g_pendingUsername = metaName;
                }
                userIdOut = userId;
                return true;
            }

            // Exchanges the stored refresh token for a fresh access token.
            inline bool RefreshAccessToken() {
                std::string rt;
                { std::lock_guard<std::mutex> lock(g_mutex); rt = g_refreshToken; }
                if (rt.empty()) return false;

                std::string body = "{\"refresh_token\":\"" + Json::Escape(rt) + "\"}";
                Response r = Request(L"POST",
                    SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token",
                    AuthHeaders(""), body);

                if (!r.transportOk || r.status < 200 || r.status >= 300) return false;

                std::string uid;
                if (!AdoptSession(r.body, uid)) return false;

                // Supabase rotates refresh tokens on every exchange. If one was
                // persisted, it has just been spent and must be replaced or the
                // next launch would try to reuse it. Only overwrite an existing
                // stored token - never create one for a user who declined
                // "remember me".
                std::string existing;
                if (LoadRefreshToken(existing)) {
                    std::string fresh;
                    { std::lock_guard<std::mutex> lock(g_mutex); fresh = g_refreshToken; }
                    // THE FIX: the result was discarded. This matters more here than a
                    // normal failed write does: the token that is still on disk has
                    // just been SPENT by the exchange above, so failing to replace it
                    // leaves a stored credential that is guaranteed to be rejected at
                    // the next launch - and nothing said so.
                    if (!fresh.empty() && !SaveRefreshToken(fresh)) {
                        SafeAddLog("ACCOUNT WARNING: Could not update the saved sign-in after refreshing it - you will have to sign in again next launch.");
                    }
                }
                return true;
            }

            // Distinguishes "your token aged out" from "you're not allowed to do
            // that". The three Supabase services word it differently and don't
            // agree on a status code, so check the body as well as the status.
            inline bool LooksLikeExpiredToken(const Response& r) {
                if (!r.transportOk) return false;
                if (r.status == 401) return true;
                if (r.status != 400 && r.status != 403) return false;

                std::string lower = r.body;
                for (auto& c : lower) c = (char)tolower((unsigned char)c);
                return lower.find("jwt expired") != std::string::npos ||
                    lower.find("invalid jwt") != std::string::npos ||
                    lower.find("token is expired") != std::string::npos ||
                    lower.find("pgrst301") != std::string::npos;
            }

            // Every authenticated call goes through here rather than Request().
            //
            // THE FIX: Supabase access tokens expire after an hour by default, and
            // the app only ever obtained one at sign-in - nothing refreshed it for
            // the life of the session. OptiMax starts with Windows and lives in the
            // tray, so "signed in this morning, changed my picture after lunch" is
            // the ordinary case, not an edge case. Every such save came back 401 and
            // was lost. One transparent refresh-and-retry closes that hole.
            inline Response AuthedRequest(const wchar_t* method,
                const std::string& url,
                std::vector<std::string> headers,
                const std::string& body,
                bool wantRawBytes = false) {
                Response r = Request(method, url, headers, body, wantRawBytes);
                if (!LooksLikeExpiredToken(r) || !RefreshAccessToken()) return r;

                // Re-stamp the Authorization header with the new token. Retried
                // exactly once: if a token minted seconds ago is also rejected, the
                // problem isn't expiry and looping would only delay the error.
                std::string token = CurrentAccessToken();
                for (auto& h : headers) {
                    if (h.compare(0, 15, "Authorization: ") == 0) {
                        h = "Authorization: Bearer " + token;
                    }
                }
                return Request(method, url, headers, body, wantRawBytes);
            }

            // Reading a profile row has three outcomes, not two. "Missing" is a
            // normal, recoverable state: Supabase Auth creates the auth user, but
            // nothing creates the profile row automatically, so a perfectly valid
            // account can legitimately have no row yet. Collapsing that into
            // "failed" is what made the old code give up silently.
            //
            // THE FIX: there used to be no distinction between "no row exists" and
            // "a row exists but its username is blank", because the only test was
            // whether the username came back empty. Those need opposite repairs: the
            // first needs an INSERT, the second needs an UPDATE. Treating the second
            // as the first made EnsureProfile POST a row whose id is already the
            // primary key, which PostgREST rejects with a duplicate-key 409 - so a
            // user in that state saw a database error every single sign-in, and could
            // never upload a profile picture (UploadAvatar calls EnsureProfile first
            // and refuses to continue when it fails).
            enum class ProfileFetch { Ok, Missing, NoUsername, Failed };

            // GET the signed-in user's profile row. RLS already restricts this to
            // their own row; the explicit id filter is defence in depth and makes
            // the intent obvious when reading the code.
            inline ProfileFetch FetchProfile(std::string& errorOut) {
                std::string token = CurrentAccessToken();
                std::string uid = CurrentUserId();
                if (token.empty() || uid.empty()) return ProfileFetch::Failed;

                std::string url = SUPABASE_URL + "/rest/v1/profiles?select=username,avatar_url&id=eq." + uid;
                Response r = AuthedRequest(L"GET", url, AuthHeaders(token), "");

                if (!r.transportOk || r.status < 200 || r.status >= 300) {
                    errorOut = FriendlyError(r, "profile");
                    return ProfileFetch::Failed;
                }

                // PostgREST answers with an array. No row for this id means an
                // empty one - a 200 with "[]", not an error status.
                const bool hasRow = r.body.find('{') != std::string::npos;
                if (!hasRow) return ProfileFetch::Missing;

                std::string username, avatar;
                Json::GetString(r.body, "username", username);
                Json::GetString(r.body, "avatar_url", avatar);

                // The avatar path is stored whatever the username turns out to be.
                // Reading it only on the success path meant a user whose row had a
                // picture but no name yet had their picture silently dropped - the
                // function returned before it ever looked at avatar_url.
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_avatarPath = avatar;
                    if (!username.empty()) g_username = username;
                }

                return username.empty() ? ProfileFetch::NoUsername : ProfileFetch::Ok;
            }

            // Load the profile, creating the row first if it doesn't exist yet.
            //
            // THE FIX: the row used to be written in exactly one place - SignUp,
            // and only when signup happened to return a session. With "Confirm
            // email" on (Supabase's default) it never does, so the username the
            // user typed was dropped on the floor and every later sign-in found no
            // row. That is why the account showed no name and no picture no matter
            // how many times they signed in. The chosen name now travels with the
            // signup request as user metadata, and this creates the row from it on
            // the first sign-in that has a real session to write with.
            //
            // Returns false with an empty errorOut when there simply isn't a name
            // to use - not a failure, just an account that still needs one picked.
            inline bool EnsureProfile(const std::string& preferredName, std::string& errorOut) {
                ProfileFetch res = FetchProfile(errorOut);
                if (res == ProfileFetch::Ok) return true;
                if (res == ProfileFetch::Failed) return false;

                std::string want = preferredName;
                if (want.empty()) {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    want = g_pendingUsername;
                }
                if (want.empty()) {
                    errorOut.clear();
                    return false;
                }

                std::string token = CurrentAccessToken();
                std::string uid = CurrentUserId();
                if (token.empty() || uid.empty()) return false;

                std::vector<std::string> h = AuthHeaders(token);
                h.push_back("Prefer: return=minimal");

                Response r;
                if (res == ProfileFetch::NoUsername) {
                    // The row is already there - fill in its username. POSTing here
                    // would collide with the existing primary key.
                    std::string body = "{\"username\":\"" + Json::Escape(want) + "\"}";
                    r = AuthedRequest(L"PATCH", SUPABASE_URL + "/rest/v1/profiles?id=eq." + uid, h, body);
                }
                else {
                    // Inserted AS the user, so the RLS insert policy (auth.uid() = id)
                    // is satisfied.
                    std::string body = "{\"id\":\"" + Json::Escape(uid) +
                        "\",\"username\":\"" + Json::Escape(want) + "\"}";
                    r = AuthedRequest(L"POST", SUPABASE_URL + "/rest/v1/profiles", h, body);
                }

                if (!r.transportOk || r.status < 200 || r.status >= 300) {
                    errorOut = FriendlyError(r, "profile");
                    return false;
                }

                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_username = want;
                    g_pendingUsername.clear();
                }
                return true;
            }

            inline void FetchAvatarBytes() {
                std::string token = CurrentAccessToken();
                std::string path;
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    path = g_avatarPath;
                }
                if (token.empty() || path.empty()) return;

                std::string url = SUPABASE_URL + "/storage/v1/object/avatars/" + path;
                Response r = AuthedRequest(L"GET", url, AuthHeaders(token, false), "", true);
                if (!r.transportOk || r.status < 200 || r.status >= 300 || r.raw.empty()) return;

                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_avatarBytes = r.raw;
                }
                g_avatarChanged = true;
            }

            // THE FIX: every operation below runs on a DETACHED std::thread, and an
            // exception that reaches the entry point of a thread does not fail that
            // thread's work - it calls std::terminate and takes the whole application
            // down, with no dialog and nothing in the log. These bodies build strings,
            // parse JSON, read files off disk and decode images through WIC; any of
            // that can throw std::bad_alloc or worse on a bad input. Just as important,
            // each one ends by clearing g_busy: a thread that dies halfway through
            // leaves the sign-in screen spinning and permanently disabled, because
            // nothing else ever resets that flag.
            template <typename F>
            inline void GuardedAuthTask(F&& fn) {
                try {
                    fn();
                    return;
                }
                catch (const std::exception& e) {
                    SetError(std::string("Something went wrong: ") + e.what());
                }
                catch (...) {
                    SetError("Something went wrong. Please try again.");
                }

                int s = g_state.load();
                if (s == (int)State::Working || s == (int)State::Restoring) {
                    g_state = (int)State::SignedOut;
                }
                g_busy = false;
            }

        } // namespace detail

        // ---------------- public accessors ----------------

        inline State GetState() { return (State)g_state.load(); }
        inline bool IsBusy() { return g_busy.load(); }

        // True while the launch screen is what's on screen: the form is up, or the
        // user's submission is in flight. Restoring is deliberately excluded - a
        // silent refresh asks nothing, so the startup phase covers it.
        //
        // Lives here rather than on the app class because the window procedure
        // needs the same answer (closing the launch screen exits outright) and it
        // has no access to the app instance.
        inline bool IsLaunchScreenActive() {
            State s = GetState();
            return s == State::SignedOut || s == State::Working;
        }

        inline std::string GetUsername() {
            std::lock_guard<std::mutex> lock(detail::g_mutex);
            return detail::g_username;
        }
        inline std::string GetEmail() {
            std::lock_guard<std::mutex> lock(detail::g_mutex);
            return detail::g_email;
        }
        // Something to call the user in a log line. The username is the point, but
        // it can legitimately be blank on an account that hasn't picked one yet,
        // and "Signed in as ." helps nobody.
        inline std::string DisplayName() {
            std::lock_guard<std::mutex> lock(detail::g_mutex);
            if (!detail::g_username.empty()) return detail::g_username;
            if (!detail::g_email.empty()) return detail::g_email;
            return "your account";
        }
        inline std::string GetAvatarPath() {
            std::lock_guard<std::mutex> lock(detail::g_mutex);
            return detail::g_avatarPath;
        }
        inline std::string GetLastError() {
            std::lock_guard<std::mutex> lock(detail::g_mutex);
            return detail::g_lastError;
        }
        inline std::string GetLastNotice() {
            std::lock_guard<std::mutex> lock(detail::g_mutex);
            return detail::g_lastNotice;
        }
        inline void ClearLastError() {
            std::lock_guard<std::mutex> lock(detail::g_mutex);
            detail::g_lastError.clear();
        }
        inline void ClearMessages() {
            std::lock_guard<std::mutex> lock(detail::g_mutex);
            detail::g_lastError.clear();
            detail::g_lastNotice.clear();
        }
        inline std::vector<unsigned char> TakeAvatarBytes() {
            std::lock_guard<std::mutex> lock(detail::g_mutex);
            return detail::g_avatarBytes;
        }

        // True if there is a stored, decryptable refresh token. Purely local and
        // fast (registry read + DPAPI) - no network - so the caller can decide
        // the initial window size before creating the window, without a stall.
        inline bool HasStoredSession() {
            std::string token;
            return detail::LoadRefreshToken(token);
        }

        // ---------------- operations (all async) ----------------

        inline void SignIn(const std::string& email, const std::string& password, bool remember) {
            bool expected = false;
            if (!g_busy.compare_exchange_strong(expected, true)) return;

            ClearMessages();
            g_state = (int)State::Working;

            std::thread([email, password, remember]() { detail::GuardedAuthTask([&] {
                std::string body = "{\"email\":\"" + Json::Escape(email) +
                    "\",\"password\":\"" + Json::Escape(password) + "\"}";

                std::string url = SUPABASE_URL + "/auth/v1/token?grant_type=password";
                detail::Response r = detail::Request(L"POST", url, detail::AuthHeaders(""), body);

                if (!r.transportOk || r.status < 200 || r.status >= 300) {
                    if (detail::IsEmailConfirmationMessage(r)) {
                        detail::SetNotice("Almost there - open the verification link we emailed you, then sign in.");
                    }
                    else {
                        detail::SetError(detail::FriendlyError(r, "signin"));
                    }
                    g_state = (int)State::SignedOut;
                    g_busy = false;
                    return;
                }

                std::string uid;
                if (!detail::AdoptSession(r.body, uid)) {
                    detail::SetError("Signed in, but the server's response was missing a session token.");
                    g_state = (int)State::SignedOut;
                    g_busy = false;
                    return;
                }

                std::string profileErr;
                if (!detail::EnsureProfile("", profileErr)) {
                    // Auth succeeded; only the profile row is unreachable, or there
                    // was no name to create it with. Let them in rather than
                    // blocking on it, but say why the username is blank.
                    if (!profileErr.empty()) detail::SetError(profileErr);
                    else detail::SetNotice("Signed in. Pick a username under Edit Profile to finish your account.");
                }

                if (remember) {
                    std::string rt;
                    { std::lock_guard<std::mutex> lock(detail::g_mutex); rt = detail::g_refreshToken; }
                    if (!rt.empty() && !detail::SaveRefreshToken(rt)) {
                        SafeAddLog("ACCOUNT WARNING: Could not securely store the session - you'll need to sign in again next launch.");
                    }
                }
                else {
                    detail::ClearRefreshToken();
                }

                detail::FetchAvatarBytes();

                g_state = (int)State::SignedIn;
                g_justAuthenticated = true;
                g_busy = false;
                SafeAddLog("ACCOUNT: Signed in as " + DisplayName() + ".");
                }); }).detach();
        }

        inline void SignUp(const std::string& email, const std::string& password,
            const std::string& username, bool remember) {
            bool expected = false;
            if (!g_busy.compare_exchange_strong(expected, true)) return;

            ClearMessages();
            g_state = (int)State::Working;

            std::thread([email, password, username, remember]() { detail::GuardedAuthTask([&] {
                // "data" becomes the auth user's metadata. Sending the username
                // here is what makes it survive email confirmation: the profile row
                // usually can't be written at this point (no session), and without
                // this the chosen name would be gone the moment this thread ends.
                std::string body = "{\"email\":\"" + Json::Escape(email) +
                    "\",\"password\":\"" + Json::Escape(password) +
                    "\",\"data\":{\"username\":\"" + Json::Escape(username) + "\"}}";

                {
                    std::lock_guard<std::mutex> lock(detail::g_mutex);
                    detail::g_pendingUsername = username;
                }

                std::string url = SUPABASE_URL + "/auth/v1/signup";
                detail::Response r = detail::Request(L"POST", url, detail::AuthHeaders(""), body);

                if (!r.transportOk || r.status < 200 || r.status >= 300) {
                    detail::SetError(detail::FriendlyError(r, "signup"));
                    g_state = (int)State::SignedOut;
                    g_busy = false;
                    return;
                }

                std::string uid;
                if (!detail::AdoptSession(r.body, uid)) {
                    // Happens when the project requires email confirmation: the user
                    // exists but there's no session yet, so we can't insert the
                    // profile row as them. Nothing went wrong here - this is the
                    // happy path for a confirmed-email project, so it goes to the
                    // notice channel rather than being painted red like a failure.
                    detail::SetNotice("Account created. Open the verification link in your email, then sign in - "
                        "your username is saved and will be waiting for you.");
                    g_signupNeedsConfirmation = true;
                    g_state = (int)State::SignedOut;
                    g_busy = false;
                    SafeAddLog("ACCOUNT: Account created for " + username + " - awaiting email verification.");
                    return;
                }

                // Signup returned a session, so the profile row can be written right
                // now. EnsureProfile also covers the case where the project has a
                // server-side trigger that already created it.
                std::string profileErr;
                if (!detail::EnsureProfile(username, profileErr)) {
                    // The auth user DID get created, so this is not a hard failure -
                    // it's "finish setting up your profile", per the spec.
                    if (!profileErr.empty()) {
                        detail::SetError(profileErr + " Your account exists - you can set a username from Edit Profile.");
                    }

                    if (remember) {
                        std::string rt;
                        { std::lock_guard<std::mutex> lock(detail::g_mutex); rt = detail::g_refreshToken; }
                        // The other three "remember me" sites report this failure; this
                        // one silently did not, so the same problem was visible or
                        // invisible depending on which branch signup happened to take.
                        if (!rt.empty() && !detail::SaveRefreshToken(rt)) {
                            SafeAddLog("ACCOUNT WARNING: Could not securely store the session - you'll need to sign in again next launch.");
                        }
                    }
                    g_state = (int)State::SignedIn;
                    g_justAuthenticated = true;
                    g_busy = false;
                    return;
                }

                if (remember) {
                    std::string rt;
                    { std::lock_guard<std::mutex> lock(detail::g_mutex); rt = detail::g_refreshToken; }
                    if (!rt.empty() && !detail::SaveRefreshToken(rt)) {
                        SafeAddLog("ACCOUNT WARNING: Could not securely store the session - you'll need to sign in again next launch.");
                    }
                }
                else {
                    detail::ClearRefreshToken();
                }

                g_state = (int)State::SignedIn;
                g_justAuthenticated = true;
                g_busy = false;
                SafeAddLog("ACCOUNT: Created account and signed in as " + username + ".");
                }); }).detach();
        }

        // Silent re-login on launch. Only shows the sign-in form if this fails.
        inline void TryRestoreSession() {
            bool expected = false;
            if (!g_busy.compare_exchange_strong(expected, true)) return;

            g_state = (int)State::Restoring;

            std::thread([]() { detail::GuardedAuthTask([&] {
                std::string stored;
                if (!detail::LoadRefreshToken(stored)) {
                    g_state = (int)State::SignedOut;
                    g_busy = false;
                    return;
                }

                std::string body = "{\"refresh_token\":\"" + Json::Escape(stored) + "\"}";
                std::string url = SUPABASE_URL + "/auth/v1/token?grant_type=refresh_token";
                detail::Response r = detail::Request(L"POST", url, detail::AuthHeaders(""), body);

                if (!r.transportOk || r.status < 200 || r.status >= 300) {
                    // Expired or revoked - drop it so we don't retry forever, and
                    // fall back to the sign-in form.
                    if (r.transportOk) detail::ClearRefreshToken();
                    g_state = (int)State::SignedOut;
                    g_busy = false;
                    SafeAddLog("ACCOUNT: Stored session is no longer valid - please sign in again.");
                    return;
                }

                std::string uid;
                if (!detail::AdoptSession(r.body, uid)) {
                    g_state = (int)State::SignedOut;
                    g_busy = false;
                    return;
                }

                // Supabase rotates refresh tokens, so persist the new one or the
                // next launch would try to reuse a spent token.
                std::string rt;
                { std::lock_guard<std::mutex> lock(detail::g_mutex); rt = detail::g_refreshToken; }
                // Same reasoning as in RefreshAccessToken: the stored token has just
                // been spent, so a failure to replace it means the next launch cannot
                // restore this session. Unchecked, that reads to the user as the app
                // randomly forgetting them.
                if (!rt.empty() && !detail::SaveRefreshToken(rt)) {
                    SafeAddLog("ACCOUNT WARNING: Could not re-save the sign-in after restoring it - you will have to sign in again next launch.");
                }

                std::string profileErr;
                detail::EnsureProfile("", profileErr);
                detail::FetchAvatarBytes();

                g_state = (int)State::SignedIn;
                g_justAuthenticated = true;
                g_busy = false;
                SafeAddLog("ACCOUNT: Welcome back, " + DisplayName() + ".");
                }); }).detach();
        }

        inline void SignOut() {
            // Tell the server, not just ourselves. Deleting the local copy stops
            // THIS app using the refresh token, but until GoTrue is told, the
            // token stays valid - so "sign out" only revoked access on the
            // machine that already had it. Fire-and-forget on a background
            // thread: signing out must not block on the network, and a failure
            // here doesn't change the local outcome.
            std::string token = detail::CurrentAccessToken();
            if (!token.empty()) {
                std::thread([token]() {
                    // Best-effort: nothing waits on this and there is nothing to
                    // report, but it still must not throw out of a thread.
                    try {
                        detail::Request(L"POST", SUPABASE_URL + "/auth/v1/logout",
                            detail::AuthHeaders(token), "{}");
                    }
                    catch (...) {}
                    }).detach();
            }

            bool cleared = detail::ClearRefreshToken();
            {
                std::lock_guard<std::mutex> lock(detail::g_mutex);
                // Wipe the in-memory copies too, not just the stored one.
                SecureZeroMemory(detail::g_accessToken.data(), detail::g_accessToken.size());
                SecureZeroMemory(detail::g_refreshToken.data(), detail::g_refreshToken.size());
                detail::g_accessToken.clear();
                detail::g_refreshToken.clear();
                detail::g_userId.clear();
                detail::g_username.clear();
                detail::g_email.clear();
                detail::g_avatarPath.clear();
                detail::g_avatarBytes.clear();
                detail::g_pendingUsername.clear();
                detail::g_lastError.clear();
                detail::g_lastNotice.clear();
            }
            g_avatarChanged = true;
            g_state = (int)State::SignedOut;
            if (cleared) {
                SafeAddLog("ACCOUNT: Signed out.");
            }
            else {
                // Say it plainly. This app is signed out as far as its own memory
                // goes, but the credential is still sitting in the registry and the
                // next launch will silently sign back in with it.
                SafeAddLog("ACCOUNT WARNING: Signed out of this session, but the saved sign-in could NOT be removed from the registry - this PC may sign in again automatically on the next launch.");
            }
        }

        inline void ContinueAsGuest() {
            ClearMessages();
            g_state = (int)State::Guest;
            g_justAuthenticated = true;
            SafeAddLog("ACCOUNT: Continuing without an account. All tweaks work normally.");
        }

        inline void UpdateUsername(const std::string& newName) {
            bool expected = false;
            if (!g_busy.compare_exchange_strong(expected, true)) return;

            ClearMessages();

            std::thread([newName]() { detail::GuardedAuthTask([&] {
                std::string token = detail::CurrentAccessToken();
                std::string uid = detail::CurrentUserId();
                if (token.empty() || uid.empty()) {
                    detail::SetError("You need to be signed in to change your username.");
                    g_busy = false;
                    return;
                }

                // Upsert rather than PATCH. A PATCH against a row that doesn't
                // exist matches nothing and still returns 204, so Edit Profile
                // reported success and saved nothing - the exact trap that left
                // accounts permanently nameless. "merge-duplicates" resolves on the
                // primary key, so this inserts the row or updates the existing one.
                std::string body = "{\"id\":\"" + Json::Escape(uid) +
                    "\",\"username\":\"" + Json::Escape(newName) + "\"}";
                std::string url = SUPABASE_URL + "/rest/v1/profiles";

                std::vector<std::string> h = detail::AuthHeaders(token);
                h.push_back("Prefer: resolution=merge-duplicates,return=minimal");

                detail::Response r = detail::AuthedRequest(L"POST", url, h, body);
                if (!r.transportOk || r.status < 200 || r.status >= 300) {
                    detail::SetError(detail::FriendlyError(r, "profile"));
                    g_busy = false;
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(detail::g_mutex);
                    detail::g_username = newName;
                    detail::g_pendingUsername.clear();
                }
                // Local state is only updated after the server confirmed the
                // write, so this says "saved" and means it.
                detail::SetNotice("Username saved.");
                SafeAddLog("ACCOUNT: Username changed to " + newName + ".");
                g_busy = false;
                }); }).detach();
        }

        inline void UploadAvatar(const std::wstring& localPath) {
            bool expected = false;
            if (!g_busy.compare_exchange_strong(expected, true)) return;

            ClearMessages();

            std::thread([localPath]() { detail::GuardedAuthTask([&] {
                std::string token = detail::CurrentAccessToken();
                std::string uid = detail::CurrentUserId();
                if (token.empty() || uid.empty()) {
                    detail::SetError("You need to be signed in to change your picture.");
                    g_busy = false;
                    return;
                }

                // There has to be a profile row to hang the picture off. Checked
                // before the upload rather than after, so a missing row costs the
                // user nothing instead of pushing an image the app then can't
                // point at.
                std::string ensureErr;
                if (!detail::EnsureProfile("", ensureErr)) {
                    detail::SetError(ensureErr.empty()
                        ? "Choose a username and save it first - your picture is stored against it."
                        : ensureErr);
                    g_busy = false;
                    return;
                }

                HANDLE hFile = CreateFileW(localPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile == INVALID_HANDLE_VALUE) {
                    detail::SetError("Couldn't open that image file.");
                    g_busy = false;
                    return;
                }

                LARGE_INTEGER size{};
                GetFileSizeEx(hFile, &size);

                // 5 MB cap - an avatar has no business being larger, and this
                // bounds both the upload and our own allocation.
                const LONGLONG kMaxAvatar = 5 * 1024 * 1024;
                if (size.QuadPart <= 0 || size.QuadPart > kMaxAvatar) {
                    CloseHandle(hFile);
                    detail::SetError("That image is too large - please choose one under 5 MB.");
                    g_busy = false;
                    return;
                }

                std::vector<unsigned char> bytes((size_t)size.QuadPart);
                DWORD read = 0;
                BOOL readOk = ReadFile(hFile, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
                CloseHandle(hFile);

                if (!readOk || read != bytes.size()) {
                    detail::SetError("Couldn't read that image file.");
                    g_busy = false;
                    return;
                }

                std::wstring lower = localPath;
                for (auto& c : lower) c = (wchar_t)towlower(c);
                bool isPng = lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".png") == 0;
                std::string contentType = isPng ? "image/png" : "image/jpeg";
                std::string fileName = isPng ? "avatar.png" : "avatar.jpg";

                // Path is always <uid>/... so the storage RLS policy restricting a
                // user to their own folder is satisfied.
                std::string objectPath = uid + "/" + fileName;
                std::string url = SUPABASE_URL + "/storage/v1/object/avatars/" + objectPath;

                std::vector<std::string> h;
                h.push_back("apikey: " + SUPABASE_PUBLISHABLE_KEY);
                h.push_back("Authorization: Bearer " + token);
                h.push_back("Content-Type: " + contentType);
                // Overwrite the previous avatar instead of accumulating files.
                h.push_back("x-upsert: true");

                std::string payload((const char*)bytes.data(), bytes.size());
                detail::Response r = detail::AuthedRequest(L"POST", url, h, payload);

                if (!r.transportOk || r.status < 200 || r.status >= 300) {
                    std::string err = detail::FriendlyError(r, "storage");
                    if (r.status == 404) err = "The 'avatars' storage bucket doesn't exist on the server yet.";
                    detail::SetError(err);
                    g_busy = false;
                    return;
                }

                // Point the profile row at the new object.
                std::string patchBody = "{\"avatar_url\":\"" + Json::Escape(objectPath) + "\"}";
                std::vector<std::string> ph = detail::AuthHeaders(token);
                ph.push_back("Prefer: return=minimal");

                detail::Response pr = detail::AuthedRequest(L"PATCH",
                    SUPABASE_URL + "/rest/v1/profiles?id=eq." + uid, ph, patchBody);

                if (!pr.transportOk || pr.status < 200 || pr.status >= 300) {
                    detail::SetError(detail::FriendlyError(pr, "profile"));
                    g_busy = false;
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(detail::g_mutex);
                    detail::g_avatarPath = objectPath;
                    detail::g_avatarBytes = bytes;
                }
                g_avatarChanged = true;

                // Housekeeping: the object name carries the file extension, so
                // "x-upsert" only overwrites when the format stays the same.
                // Switching from a PNG to a JPG (or back) writes a new object and
                // strands the old one in the bucket with nothing pointing at it.
                //
                // Ordered deliberately last. avatar_url already names the new
                // object and the new bytes are already live locally, so the only
                // file this can remove is one nothing refers to. It is also
                // best-effort on purpose: a failure here is litter in a bucket,
                // not something worth turning a successful upload into an error
                // over, so the response is not inspected.
                std::string stalePath = uid + "/" + (isPng ? "avatar.jpg" : "avatar.png");
                if (stalePath != objectPath) {
                    detail::AuthedRequest(L"DELETE",
                        SUPABASE_URL + "/storage/v1/object/avatars/" + stalePath,
                        detail::AuthHeaders(token, false), "");
                }

                detail::SetNotice("Profile picture saved.");
                SafeAddLog("ACCOUNT: Profile picture updated.");
                g_busy = false;
                }); }).detach();
        }

    } // namespace Auth
} // namespace OptiMax
