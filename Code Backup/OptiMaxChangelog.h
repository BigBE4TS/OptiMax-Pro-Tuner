#pragma once
#include <vector>
#include <string>
#include "OptiMaxVersion.h"

// ==========================================================================
// WHAT'S NEW - the short changelog shown once after an update.
//
// WHY THE TEXT IS COMPILED IN rather than fetched. Downloading it would add a
// network dependency, a failure mode, and the trust question OptiMaxCloudSync.h
// documents at length: profiles.db is remotely editable, so nothing remotely
// editable is allowed to drive anything. It would also let a release ship with
// no notes, which is exactly what happened to v4.2.1 - release.ps1 hardcoded
// --generate-notes and the public repo's history is release-artefact commits,
// so the notes came out near-empty and omitted the mandatory throttling caveat.
// Compiling the text in means a release physically cannot be built without
// someone having written it.
//
// WHY IT LIVES NEXT TO THE VERSION. kEntries is keyed to
// OPTIMAX_VERSION_STRING, and the version string used to live in ten places
// before v4.2.2 collapsed it to one. A changelog that can disagree with the
// version it describes is that same bug wearing a different hat, so the check
// below is a hard one: if the notes were not updated for this version, the
// prompt does not appear at all rather than showing the previous release's
// notes under the new number.
//
// KEEP EACH LINE SHORT. This is a prompt someone dismisses in five seconds, not
// the release notes. The full notes live in the GitHub release.
// ==========================================================================

namespace OptiMax {
    namespace Changelog {

        // The version these notes describe. Compared against
        // OPTIMAX_VERSION_STRING at runtime so a forgotten update is caught
        // rather than silently mislabelled.
        inline const char* kNotesForVersion = "4.3.0";

        // ⚠️ THESE MUST BE REWRITTEN WHENEVER THE RELEASE GAINS A FEATURE, not
        // only when the version number changes. The guard below catches a
        // forgotten VERSION bump; nothing catches notes that are merely stale
        // for a version that is still correct - which is exactly what happened
        // here. 4.3.0's notes were written when it was an attribution release
        // and still said so after the entire interface had been rebuilt, so the
        // version check passed and the prompt would have shown a user an
        // accurate description of a release that no longer existed.
        inline const std::vector<std::string>& Entries() {
            static const std::vector<std::string> entries = {
                "OptiMax now says WHY your game stuttered - a Defender scan, a display driver "
                "reset, a disk timeout, or another program hogging the CPU.",

                "When it is another program, OptiMax names it - \"chrome.exe (14 processes, "
                "62%)\" - instead of shrugging. And when the hitch was OptiMax's own doing, it "
                "says so rather than blaming Windows for it.",

                "And what to do about it: each stall carries a plain-language suggestion, "
                "with a button where OptiMax has a tweak that genuinely helps. Turn on "
                "Auto-fix stalls in Settings to let it apply the safe ones for you.",

                "Rebuilt interface. Eleven pages became six, and every tweak now lives on one "
                "searchable Tweaks page grouped by what it does rather than by which part of "
                "Windows it touches.",

                "Live Monitoring gained proper dials and a 60-second history graph of CPU, "
                "memory and DPC latency.",

                "OptiMax also gets out of the way better: RAM cleaning is deferred while the "
                "system is already struggling, and the latency test refuses to publish a "
                "reading taken during a virus scan.",

                "Games installed in a folder with an accent or a non-English name now work. "
                "Their paths were being corrupted, so the game never matched its own library "
                "entry and Auto-Boost silently never fired for it."
            };
            return entries;
        }

        // True only when the notes above were written for the version actually
        // running. Deliberately strict: showing last release's notes under a new
        // version number is worse than showing nothing, because it is the app
        // stating something false about itself.
        inline bool NotesMatchThisBuild() {
            return std::string(kNotesForVersion) == std::string(OPTIMAX_VERSION_STRING);
        }

    }
}
