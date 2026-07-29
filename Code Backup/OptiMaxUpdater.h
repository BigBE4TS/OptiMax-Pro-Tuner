#pragma once

#include <windows.h>
#include <string>
#include <wininet.h>
#include <urlmon.h>
#include <fstream>
#include <bcrypt.h>
#include <vector>
#include <algorithm>
#include <atomic>
#include "OptiMaxGlobals.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "bcrypt.lib")

// THE FIX: this used to be a plain bool class member on OptiMaxApp, set to true
// right before launching the update thread, and never once set back to false -
// on ANY failure path (network error, missing/invalid checksum, hash mismatch,
// UAC denial), the "Downloading Update..." modal stayed open forever with no
// close button anywhere. Now a global the updater itself owns and always resets.
extern std::atomic<bool> g_isDownloadingUpdate;

namespace OptiMax {
    class Updater {
    public:
        static inline const std::string GITHUB_VERSION_URL = "https://raw.githubusercontent.com/BigBE4TS/OptiMax-Pro-Tuner/main/version.txt";
        static inline const std::string GITHUB_EXE_URL = "https://github.com/BigBE4TS/OptiMax-Pro-Tuner/releases/latest/download/OptiMax_Profiler.exe";
        // NOTE: this file MUST exist in the repo for updating to work at all. Verification
        // is mandatory - if the checksum cannot be fetched, the update is refused. That is
        // the correct behaviour, but it does mean a release with no published checksum is
        // a release nobody can install.
        //
        // Publish it with release.ps1, which computes the digest from the exact binary it
        // uploads and pushes the three artefacts in the required order (binary, then
        // checksum, then version.txt last - version.txt is the trigger). `release.ps1
        // -Check` audits what is currently published without changing anything.
        static inline const std::string GITHUB_HASH_URL = "https://raw.githubusercontent.com/BigBE4TS/OptiMax-Pro-Tuner/main/OptiMax_Profiler.exe.sha256";

        // A UTF-8 BOM is invisible in every editor and survives every whitespace
        // trim, but it is not whitespace: it reaches std::stoi as part of the
        // first number, throws, and the caller's catch treats that as major
        // version 0 - which is never newer than the running build. The result is
        // an updater that silently never fires and logs that everything is fine.
        // PowerShell's Out-File and ">" both emit a BOM by default, so this is a
        // very easy mistake to make when publishing version.txt by hand.
        static void StripUtf8Bom(std::string& s) {
            if (s.size() >= 3 &&
                (unsigned char)s[0] == 0xEF &&
                (unsigned char)s[1] == 0xBB &&
                (unsigned char)s[2] == 0xBF) {
                s.erase(0, 3);
            }
        }

        // Shared HTTP GET for the two small text files this class reads.
        //
        // THE FIX: both fetches used to call InternetReadFile exactly once and
        // treat whatever came back as the file's contents. That was wrong twice
        // over. InternetReadFile is not obliged to return the whole body in one
        // call, so a split response silently truncated the value ("4" instead of
        // "4.3.0" parses as 4.0.0 and suppresses the update); and no HTTP status
        // was ever checked, so GitHub's 404 page was parsed as though it were
        // data. The second one is what turned a missing checksum file into an
        // unexplainable failure instead of a clear message.
        //
        // statusOut carries the HTTP status back so callers can tell "this file
        // was never published" apart from "this file is malformed" - two very
        // different problems, and the user is entitled to know which one they have.
        static std::string HttpGetText(const std::string& url, size_t maxBytes, DWORD& statusOut) {
            statusOut = 0;

            HINTERNET hInternet = InternetOpenA("OptiMax AutoUpdater", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
            if (!hInternet) return "";

            // Cache buster - and A5-4: read the limits before trusting it.
            //
            // INTERNET_FLAG_RELOAD below only bypasses the LOCAL WinINet cache.
            // The query string is what a shared cache in between would key on,
            // so it is kept for intermediate/corporate proxies, which do vary
            // on the full URL.
            //
            // ⚠️ It provably does NOT work on raw.githubusercontent.com, which
            // is where these URLs actually point. That CDN keys on path alone
            // and ignores the query string. MEASURED, not assumed: during the
            // v4.2.0 publish, raw kept serving the old version.txt and the old
            // checksum for several minutes after the push while the GitHub API
            // already showed the new values. An earlier version of this comment
            // claimed the buster defeated the CDN; it does not, and a future
            // reader acting on that would mis-diagnose a propagation delay as a
            // client bug.
            //
            // The real exposure was never the ~5 minute TTL - a client seeing a
            // release a few minutes late is harmless. It was version.txt and
            // the checksum propagating INDEPENDENTLY, which briefly pairs a new
            // version with a stale hash and makes the client report tampering.
            // That is fixed at the publishing end, where it can be: release.ps1
            // now polls raw and holds STEP C until the new checksum is actually
            // being served (A5-5). Nothing the client does could have closed it.
            std::string bustedUrl = url;
            bustedUrl += (url.find('?') == std::string::npos) ? "?t=" : "&t=";
            bustedUrl += std::to_string(GetTickCount64());

            HINTERNET hConnect = InternetOpenUrlA(hInternet, bustedUrl.c_str(), NULL, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
            if (!hConnect) {
                InternetCloseHandle(hInternet);
                return "";
            }

            DWORD statusCode = 0;
            DWORD statusLen = sizeof(statusCode);
            if (!HttpQueryInfoA(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                &statusCode, &statusLen, NULL)) {
                statusCode = 0;
            }
            statusOut = statusCode;

            std::string body;
            if (statusCode == 200) {
                char buffer[1024];
                DWORD bytesRead = 0;
                // Loop until a read returns zero bytes - one call is not enough.
                while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                    body.append(buffer, bytesRead);
                    if (body.size() > maxBytes) break;  // bounded; both files are tiny
                }
            }

            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return body;
        }

        static std::string FetchLatestVersion() {
            DWORD status = 0;
            std::string body = HttpGetText(GITHUB_VERSION_URL, 256, status);
            if (status != 200) return "";

            StripUtf8Bom(body);
            size_t last = body.find_last_not_of(" \n\r\t");
            if (last == std::string::npos) return "";
            body.erase(last + 1);

            size_t first = body.find_first_not_of(" \n\r\t");
            if (first != std::string::npos && first > 0) body.erase(0, first);
            return body;
        }

        // Fetches the expected SHA-256 hex digest for the current release exe.
        // statusOut (optional) receives the HTTP status so the caller can report
        // a missing checksum file as exactly that.
        static std::string FetchExpectedHash(DWORD* statusOut = nullptr) {
            DWORD status = 0;
            std::string body = HttpGetText(GITHUB_HASH_URL, 512, status);
            if (statusOut) *statusOut = status;
            if (status != 200) return "";

            StripUtf8Bom(body);

            // THE FIX: this used to strip every non-hex character from the whole
            // file and hope what remained was the digest. That breaks on
            // certutil's default output, whose "SHA256 hash of <file>:" header
            // contributes stray hex letters of its own (a, 2, 5, 6, f...) and
            // silently corrupts the result. Scanning for a contiguous run of
            // exactly 64 hex characters handles every common format instead: a
            // bare digest, "digest *filename", or certutil's three-line output.
            std::string run;
            for (size_t i = 0; i <= body.size(); ++i) {
                if (i < body.size() && isxdigit((unsigned char)body[i])) {
                    run += (char)tolower((unsigned char)body[i]);
                }
                else {
                    if (run.size() == 64) return run;
                    run.clear();
                }
            }
            return "";
        }

        // Computes the SHA-256 of a local file using CNG (BCrypt), returned as lowercase hex.
        static std::string ComputeFileSha256(const std::string& path) {
            std::string result = "";
            BCRYPT_ALG_HANDLE hAlg = NULL;
            BCRYPT_HASH_HANDLE hHash = NULL;
            DWORD hashObjLen = 0, hashLen = 0, cbData = 0;
            std::vector<BYTE> hashObj, hashBuf;

            if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return "";

            if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjLen, sizeof(DWORD), &cbData, 0) != 0) {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return "";
            }
            hashObj.resize(hashObjLen);

            if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(DWORD), &cbData, 0) != 0) {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return "";
            }
            hashBuf.resize(hashLen);

            if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjLen, NULL, 0, 0) != 0) {
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return "";
            }

            std::ifstream file(path, std::ios::binary);
            if (!file) {
                BCryptDestroyHash(hHash);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                return "";
            }

            char buf[65536];
            bool hashOk = true;
            while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
                if (BCryptHashData(hHash, (PUCHAR)buf, (ULONG)file.gcount(), 0) != 0) { hashOk = false; break; }
            }
            file.close();

            if (hashOk && BCryptFinishHash(hHash, hashBuf.data(), hashLen, 0) == 0) {
                static const char* hex = "0123456789abcdef";
                result.reserve(hashLen * 2);
                for (BYTE b : hashBuf) {
                    result += hex[(b >> 4) & 0xF];
                    result += hex[b & 0xF];
                }
            }

            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return result;
        }

        static void DownloadAndApplyUpdate() {
            g_isDownloadingUpdate = true;

            // GetTempPathA leaves the buffer undefined when it fails, and this path is
            // where a binary gets written - it must not be built out of stack garbage.
            char tempPath[MAX_PATH] = { 0 };
            DWORD tempLen = GetTempPathA(MAX_PATH, tempPath);
            if (tempLen == 0 || tempLen > MAX_PATH) {
                SafeAddLog("UPDATER ERROR: Could not find a temporary folder to download into.");
                g_isDownloadingUpdate = false;
                return;
            }
            std::string newExePath = std::string(tempPath) + "OptiMax_UpdateTarget.exe";

            SafeAddLog("UPDATER: Downloading the new version...");

            // THE FIX: the two text fetches go through HttpGetText, which busts the
            // cache - but the binary download did not, and URLDownloadToFile reads
            // straight through the WinINet cache. The release URL ends in
            // "releases/latest/download/OptiMax_Profiler.exe", which is the SAME url
            // for every release ever published, so a cached copy of the PREVIOUS
            // version could be handed back instead of the new one. It would then be
            // checked against the NEW release's published checksum, fail, and report
            // "the downloaded file does not match the published checksum" - which
            // reads as a tampered or corrupt download and would have sent someone
            // hunting a security problem that isn't there. Evicting the entry first
            // is safe whether or not one exists, and unlike a query-string cache
            // buster it cannot alter how GitHub resolves the URL.
            DeleteUrlCacheEntryA(GITHUB_EXE_URL.c_str());

            HRESULT hr = URLDownloadToFileA(NULL, GITHUB_EXE_URL.c_str(), newExePath.c_str(), 0, NULL);
            if (hr != S_OK) {
                SafeAddLog("UPDATER ERROR: The download failed - check your internet connection and try again.");
                g_isDownloadingUpdate = false;
                return;
            }

            // THE FIX: previously this downloaded exe was executed elevated with zero
            // integrity check at all. If GitHub, the repo/account, or DNS were ever
            // compromised, whatever sat at that URL would run as admin on every user's
            // machine. Now we fetch the published SHA-256 and refuse to proceed unless
            // the downloaded file matches it exactly.
            SafeAddLog("UPDATER: Verifying the download...");
            DWORD hashStatus = 0;
            std::string expectedHash = FetchExpectedHash(&hashStatus);
            if (expectedHash.size() != 64) {
                // Three genuinely different failures used to share one message,
                // which made a release-process mistake look like a broken PC.
                if (hashStatus == 404) {
                    SafeAddLog("UPDATER ERROR: The checksum for this release has not been published, "
                        "so the download cannot be verified and will NOT be installed. This is a "
                        "problem with the release itself, not with your computer - it will resolve "
                        "once the checksum is published.");
                }
                else if (hashStatus == 0) {
                    SafeAddLog("UPDATER ERROR: Could not reach the server to verify the download. "
                        "Check your internet connection and try again.");
                }
                else {
                    SafeAddLog("UPDATER ERROR: The published checksum is not readable (server returned "
                        + std::to_string(hashStatus) + "). Stopping the update for safety.");
                }
                DeleteFileA(newExePath.c_str());
                g_isDownloadingUpdate = false;
                return;
            }

            std::string actualHash = ComputeFileSha256(newExePath);
            if (actualHash.empty()) {
                SafeAddLog("UPDATER ERROR: Could not check the downloaded file. Stopping the update for safety.");
                DeleteFileA(newExePath.c_str());
                g_isDownloadingUpdate = false;
                return;
            }

            if (actualHash != expectedHash) {
                SafeAddLog("UPDATER ERROR: The downloaded file does not match the published checksum. Stopping - this file will NOT be run.");
                DeleteFileA(newExePath.c_str());
                g_isDownloadingUpdate = false;
                return;
            }

            SafeAddLog("UPDATER: Download verified.");

            char currentExePath[MAX_PATH];
            if (GetModuleFileNameA(NULL, currentExePath, MAX_PATH) == 0) {
                SafeAddLog("UPDATER ERROR: Could not locate the running program in order to replace it.");
                DeleteFileA(newExePath.c_str());
                g_isDownloadingUpdate = false;
                return;
            }

            // THE FIX: this used to write a .bat into %TEMP% and run it through
            // cmd.exe with "runas" - a script that moved a freshly downloaded exe
            // over the running program and relaunched it. It worked, but it is also
            // the single most recognisable self-updating-dropper pattern there is,
            // and it was a significant contributor to antivirus false positives on
            // an otherwise honest binary. It also forced a second UAC prompt even
            // though this process elevated itself at startup.
            //
            // Windows permits RENAMING a running executable - it only forbids
            // deleting one or writing into it - so the swap needs no helper
            // process, no shell, and no temporary script:
            //
            //   1. move the running exe aside to <name>.old
            //   2. move the verified download into the vacated path
            //   3. start it and exit, which unloads the old image
            //
            // The leftover .old is deleted by the next launch (see wWinMain). The
            // download and checksum verification above are untouched - this changes
            // only how the verified file gets installed.
            std::string oldExePath = std::string(currentExePath) + ".old";

            // A leftover from a previous update would block step 1.
            DeleteFileA(oldExePath.c_str());

            if (!MoveFileExA(currentExePath, oldExePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                DWORD err = GetLastError();
                SafeAddLog(err == ERROR_ACCESS_DENIED
                    ? "UPDATER ERROR: Not allowed to modify the installation folder. Restart OptiMax as administrator and try the update again."
                    : "UPDATER ERROR: Could not stage the update. Nothing has been changed.");
                DeleteFileA(newExePath.c_str());
                g_isDownloadingUpdate = false;
                return;
            }

            // MOVEFILE_COPY_ALLOWED because %TEMP% is frequently on a different
            // volume from the install directory, and a rename cannot cross volumes.
            if (!MoveFileExA(newExePath.c_str(), currentExePath,
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                // Put the original back. A failed update must never leave the user
                // with no program at all - that is the one outcome worse than not
                // updating, and it is why step 1 renames rather than deletes.
                MoveFileExA(oldExePath.c_str(), currentExePath, MOVEFILE_REPLACE_EXISTING);
                SafeAddLog("UPDATER ERROR: Could not install the update. Your existing version is untouched.");
                DeleteFileA(newExePath.c_str());
                g_isDownloadingUpdate = false;
                return;
            }

            SafeAddLog("UPDATER: Update installed - restarting OptiMax.");

            // No "runas" here. This process is already elevated, and the new one
            // self-elevates on its own at startup exactly as this one did, so the
            // second prompt the old flow forced was never buying anything.
            // "-updated" tells the incoming process that the instance it is about
            // to collide with is this one, on its way out. Without it the new
            // process cannot distinguish an outgoing predecessor from a genuine
            // second instance, and the only safe assumption - wait for the mutex -
            // makes every ordinary double-click hang before the window appears.
            HINSTANCE hInst = ShellExecuteA(NULL, "open", currentExePath, "-updated", NULL, SW_SHOWNORMAL);

            if ((INT_PTR)hInst > 32) {
                exit(0);
            }

            // The swap already succeeded, so the update IS installed - this process
            // is simply still running from the renamed old image. Nothing is broken;
            // the new version comes up on the next launch.
            SafeAddLog("UPDATER: Update installed, but the new version could not be started automatically. "
                "It will be used the next time you open OptiMax.");
            g_isDownloadingUpdate = false;
        }
    };
}