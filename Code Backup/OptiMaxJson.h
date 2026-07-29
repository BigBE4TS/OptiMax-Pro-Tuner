#pragma once

#include <string>
#include <cctype>
#include <cstdlib>

// =========================================================================
// MINIMAL JSON FIELD EXTRACTION
//
// Deliberately NOT a general-purpose JSON parser. The app only ever reads a
// handful of known fields out of Supabase's own well-formed responses
// (access_token, refresh_token, user.id, username, avatar_url, error text),
// and adding a real JSON library for that would introduce the only
// third-party dependency in the entire project.
//
// It is a scanner rather than a regex/substring hunt, which matters: it walks
// string literals properly, so a value that happens to contain ':' or '"' or
// the name of another key can never be mistaken for structure. It assumes the
// input is well-formed JSON - fine for a server response, and a malformed body
// simply yields "field not found" rather than misreading anything.
// =========================================================================

namespace OptiMax {
    namespace Json {

        inline std::string Unescape(const std::string& in) {
            std::string out;
            out.reserve(in.size());

            for (size_t i = 0; i < in.size(); ++i) {
                if (in[i] != '\\') { out += in[i]; continue; }
                if (++i >= in.size()) break;

                switch (in[i]) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    // \uXXXX - decode the BMP code point to UTF-8. Surrogate
                    // pairs aren't recombined; each half becomes a replacement
                    // char rather than producing invalid UTF-8 downstream.
                    if (i + 4 >= in.size()) { i = in.size(); break; }
                    std::string hex = in.substr(i + 1, 4);
                    i += 4;
                    unsigned cp = (unsigned)strtoul(hex.c_str(), nullptr, 16);
                    if (cp >= 0xD800 && cp <= 0xDFFF) { out += '?'; break; }
                    if (cp < 0x80) {
                        out += (char)cp;
                    }
                    else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    else {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += in[i]; break;
                }
            }
            return out;
        }

        // Reads the string literal starting at the opening quote at 'pos'.
        // Returns the raw (still-escaped) contents and moves pos past the
        // closing quote.
        inline std::string ReadRawString(const std::string& j, size_t& pos) {
            std::string raw;
            if (pos >= j.size() || j[pos] != '"') return raw;
            ++pos;

            bool escaped = false;
            while (pos < j.size()) {
                char c = j[pos];
                if (escaped) { raw += c; escaped = false; }
                else if (c == '\\') { raw += c; escaped = true; }
                else if (c == '"') { ++pos; return raw; }
                else { raw += c; }
                ++pos;
            }
            return raw;
        }

        // Returns the index just past the ':' following "key", at any depth,
        // or npos. Skips string contents so keys inside values can't match.
        inline size_t FindKey(const std::string& j, const std::string& key, size_t from = 0) {
            size_t i = from;
            while (i < j.size()) {
                if (j[i] != '"') { ++i; continue; }

                size_t tokenStart = i;
                std::string token = Unescape(ReadRawString(j, i));

                size_t after = i;
                while (after < j.size() && isspace((unsigned char)j[after])) ++after;

                if (after < j.size() && j[after] == ':') {
                    if (token == key) return after + 1;
                }
                else {
                    // Not a key - it was a string value. Nothing to do; i has
                    // already advanced past it.
                    (void)tokenStart;
                }
            }
            return std::string::npos;
        }

        inline bool GetString(const std::string& j, const std::string& key, std::string& out, size_t from = 0) {
            size_t p = FindKey(j, key, from);
            if (p == std::string::npos) return false;

            while (p < j.size() && isspace((unsigned char)j[p])) ++p;
            if (p >= j.size()) return false;

            // Treat an explicit null as "absent" - avatar_url is nullable, and
            // callers want an empty string rather than the literal text "null".
            if (j.compare(p, 4, "null") == 0) { out.clear(); return true; }
            if (j[p] != '"') return false;

            out = Unescape(ReadRawString(j, p));
            return true;
        }

        // (GetNumber() lived here and was removed - zero call sites. Every JSON
        // this app parses is a Supabase auth response, whose numeric fields are
        // read as strings. Re-add it from git history if that changes.)

        // Returns the raw text of the {...} or [...] value for "key", so a
        // caller can scope a lookup. Needed because a Supabase token response
        // has both a top-level "id"-free shape and a nested "user":{"id":...} -
        // searching the whole body for "id" could otherwise pick up the wrong one.
        inline bool GetRawValue(const std::string& j, const std::string& key, std::string& out, size_t from = 0) {
            size_t p = FindKey(j, key, from);
            if (p == std::string::npos) return false;

            while (p < j.size() && isspace((unsigned char)j[p])) ++p;
            if (p >= j.size()) return false;

            char open = j[p];
            if (open != '{' && open != '[') return false;
            char close = (open == '{') ? '}' : ']';

            size_t start = p;
            int depth = 0;
            while (p < j.size()) {
                char c = j[p];
                if (c == '"') { ReadRawString(j, p); continue; }
                if (c == open) ++depth;
                else if (c == close) {
                    --depth;
                    if (depth == 0) { out = j.substr(start, (p - start) + 1); return true; }
                }
                ++p;
            }
            return false;
        }

        // Escapes a value for embedding in a request body. Without this, a
        // password or username containing a quote or backslash would produce a
        // malformed request (and, worse, could alter the JSON structure).
        inline std::string Escape(const std::string& in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (unsigned char c : in) {
                switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        sprintf_s(buf, "\\u%04x", c);
                        out += buf;
                    }
                    else out += (char)c;
                    break;
                }
            }
            return out;
        }

    } // namespace Json
} // namespace OptiMax
