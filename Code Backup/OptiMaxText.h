#pragma once
// ==========================================================================
// TEXT CONVERSION - one correct pair, replacing several lossy ones.
//
// WHAT WAS WRONG. This project converted between std::wstring and std::string
// by iterator range - `std::string(ws.begin(), ws.end())`. That is a per-
// character narrowing cast, not an encoding conversion, and it is lossy above
// U+007F. Worse than it first looks: `char` is signed on MSVC, so a character
// like 'U' with an umlaut does not merely degrade, it comes back as a negative
// value and round-trips to 0xFFDC. Any path containing a non-ASCII character -
// C:\Users\Jose\... with an accent, a Pokemon folder, most non-English user
// names - was corrupted the moment it crossed that boundary.
//
// It is NOT a localisation issue and no UI string is involved. It breaks file
// paths, which is why it breaks the game library.
//
// Those two casts are also the source of the two C4244 warnings that CLAUDE.md
// records as the expected clean-build output. They are expected because they
// were known and deferred, not because they are harmless.
//
// WHY THIS IS ITS OWN HEADER. It needs <windows.h> for the conversion API, and
// OptiMaxGlobals.h - the natural home - deliberately includes only <string>.
// Adding <windows.h> there would put it ahead of <winsock2.h> in an include
// order CLAUDE.md records as having broken this build twice. This header is
// self-contained for the same reason OptiMaxAttribution.h is, and depends on
// nothing in this project, so it cannot participate in a cycle.
//
// UTF-8 IS THE CHOSEN NARROW ENCODING, not the active code page. CP_ACP loses
// anything outside the machine's locale, so a config written on one machine
// would not read back correctly on another - and the persisted game library is
// exactly that kind of file.
// ==========================================================================
#include <windows.h>
#include <string>

namespace OptiMax {
    namespace Text {

        inline std::string ToUtf8(const std::wstring& w) {
            if (w.empty()) return std::string();
            const int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                nullptr, 0, nullptr, nullptr);
            if (need <= 0) return std::string();
            std::string out((size_t)need, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                &out[0], need, nullptr, nullptr);
            return out;
        }

        inline std::wstring FromUtf8(const std::string& s) {
            if (s.empty()) return std::wstring();
            const int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                nullptr, 0);
            if (need <= 0) return std::wstring();
            std::wstring out((size_t)need, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], need);
            return out;
        }

        // True when every character survives the narrow round trip unchanged -
        // i.e. the string is pure ASCII.
        //
        // Needed because the persisted config format predates UTF-8 here and
        // must keep loading files written by older versions. Reading is
        // therefore permissive (see the loader), but this lets callers tell an
        // ASCII entry - where old and new encodings agree exactly - from one
        // where they cannot.
        inline bool IsPureAscii(const std::wstring& w) {
            for (wchar_t c : w) {
                if (c > 0x7F) return false;
            }
            return true;
        }
        inline bool IsPureAscii(const std::string& s) {
            for (unsigned char c : s) {
                if (c > 0x7F) return false;
            }
            return true;
        }

    }
}
