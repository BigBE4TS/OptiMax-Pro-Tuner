#pragma once

#include <string>
#include "OptiMaxAttribution.h"

// ==========================================================================
// STALL REMEDIES - turning "what held your machine up" into "what to do".
//
// v4.3.0 made the app able to say WHY a stall happened. This turns that into
// something the user can act on, which is the half that actually helps: a
// cause with no next step is a better error message, not a fix.
//
// 🔴 THE HONEST MAPPING IS MOSTLY EMPTY, AND THAT IS THE POINT.
//
// Of the five causes attribution can detect, exactly ONE maps cleanly onto a
// tweak this app owns:
//
//   Defender scan   - the most common cause, and the 2026-07-28 incident.
//                     Already mitigated as far as this project permits: the
//                     GameSession power request shipped in 4.2.1 asks Windows
//                     to defer idle-time maintenance. Beyond that the remedy
//                     is a Defender exclusion, and CLAUDE.md forbids this app
//                     reconfiguring Defender - it is textbook malware
//                     behaviour and would plausibly void the AV clearance.
//                     ADVICE ONLY, phrased for the user to act on themselves.
//   Display reset   - a TDR. Disabling MPO is a real documented workaround
//                     for some driver/monitor combinations, but it is not a
//                     general TDR fix, and the honest first suggestions are a
//                     driver update and removing any overclock.
//   Disk reset      - a hardware timeout. A failing drive, a bad cable, or a
//   Disk retry        storage driver bug. NOTHING this app toggles fixes any
//                     of those. ⚠️ Do not "helpfully" suggest disabling
//                     SysMain and Search here: those reduce background disk
//                     CONTENTION, and a 129/153 is a TIMEOUT. Conflating the
//                     two would produce a confident wrong answer, which is
//                     the failure this whole component was built to avoid.
//   Power transition- the one clean mapping. Core parking and the energy
//                     performance preference are exactly what stops the CPU
//                     dropping state mid-game.
//
// So four of five are advice, and one is a button. Resisting the urge to
// invent a tweak for each cause is what keeps this trustworthy - a
// recommendation the user follows and does not benefit from costs more
// credibility than saying "this one is not something OptiMax can fix".
//
// 🔴 WHY Action IS AN ENUM AND NOT A FUNCTION POINTER OR A FLAG REFERENCE.
// The tweak flags (g_isCoreActive and friends) are declared in OptiMaxApp.h
// and do not exist this far down the include order - the dependency that has
// broken this build twice. This header decides WHAT should happen; the app
// layer, which can see the flags, decides whether it is already done and
// carries it out.
// ==========================================================================

namespace OptiMax {
    namespace Diagnostics {
        namespace Remedy {

            enum class Action {
                None = 0,            // advice only - nothing this app owns helps
                CoreTweaks,          // timer, GPU scheduling, network throttling
                KeepCoresAwake,      // core parking off + EPP to performance
                AutoBoost,           // apply the tweaks when a game starts
                DisableMPO,          // multi-plane overlay off  (NEEDS A REBOOT)
            };

            struct Advice {
                // One line, imperative, and specific enough to act on.
                const char* headline = "";
                // Why, and the caveat. This is where honesty about what the
                // suggestion will and will not achieve belongs.
                const char* detail = "";

                Action action = Action::None;

                // Written but only read at boot. A reboot-gated tweak can never
                // fix the stall that suggested it, so the UI must say so rather
                // than implying the next frame will be better.
                bool needsReboot = false;

                // 🔴 SAFE TO APPLY WITHOUT ASKING, when the user has opted in.
                // Deliberately a separate flag from `action` rather than derived
                // from it, because the two questions are different: "is there
                // something to do" and "may we do it unattended".
                //
                // A tweak qualifies only if it is reversible, takes effect
                // immediately, and does not change the security posture of the
                // machine. Nothing reboot-gated is ever in this set.
                bool autoSafe = false;

                bool Actionable() const { return action != Action::None; }
            };

            // The mapping. Takes the kind rather than the Report so it can be
            // called from a stored record without keeping the whole report.
            inline Advice ForCause(Attribution::CauseKind kind) {
                Advice a;
                switch (kind) {

                case Attribution::CauseKind::OptiMaxItself:
                    a.headline = "This one was OptiMax - no action needed";
                    a.detail =
                        "The stall lines up with work OptiMax was doing itself: taking a system "
                        "restore point, trimming memory, or applying a tweak. Those are one-off "
                        "costs paid at the moment you asked for them (or, if automatic fixes are "
                        "on, when a previous stall triggered one), and they do not repeat on "
                        "their own.\n\n"
                        "Nothing here needs fixing. It is listed because a tuning tool that "
                        "quietly blamed Windows for its own hitches would be worth less than one "
                        "that admits them.";
                    // 🔴 NO ACTION, AND NEVER AUTO-SAFE - this is the gate that
                    // stops a feedback loop. Applying core tweaks takes a restore
                    // point, a restore point can stall the machine, and a stall
                    // triggers the automatic remedy. Left auto-safe, OptiMax would
                    // answer its own hitch by causing another one. The stall is
                    // real and worth reporting; the response to it is to do
                    // nothing.
                    a.autoSafe = false;
                    break;

                case Attribution::CauseKind::ProcessContention:
                    a.headline = "Another program was competing for the CPU";
                    a.detail =
                        "The processor was fully committed during the stall, and the stall "
                        "report names which programs were using it. Nothing was broken - there "
                        "was simply more work queued than there are cores to run it, so your "
                        "game had to wait its turn.\n\n"
                        "Closing the program named above is the direct fix. If it is something "
                        "you want left running - a download, a stream encoder, a compile - turn "
                        "on Background throttling, which drops everything except your game to a "
                        "lower priority. ⚠️ It asks first, because it slows those programs down "
                        "on purpose, and that is a trade only you can make.";
                    // Deliberately NO action, and specifically not an automatic
                    // one. Background throttling is the honest fix and it is
                    // exactly the kind of change that must not happen unattended:
                    // it deprioritises every background program on the machine,
                    // which is why the toggle already carries a confirmation
                    // dialog. Applying it silently would route around a consent
                    // step the UI deliberately added.
                    a.autoSafe = false;
                    break;

                case Attribution::CauseKind::DefenderScan:
                case Attribution::CauseKind::RawScanEvent:
                    a.headline = "Add your game's folder to Defender's exclusions";
                    a.detail =
                        "OptiMax already asks Windows to hold off idle-time maintenance while "
                        "you play, and that is as far as it will go - reconfiguring Defender "
                        "from inside a tuning tool is behaviour antivirus software is right to "
                        "distrust, so this one is yours to do.\n\n"
                        "Windows Security > Virus & threat protection > Manage settings > "
                        "Exclusions, and add the folder your game is installed in. Scheduled "
                        "scans will then skip it.";
                    // No action. This is the most common cause and it is the one
                    // with no button, which is uncomfortable and correct.
                    break;

                case Attribution::CauseKind::DisplayReset:
                    a.headline = "Update your GPU driver, and remove any overclock";
                    a.detail =
                        "The display driver stopped responding and Windows reset it (a TDR). "
                        "That is a driver, hardware or overclock problem, not a Windows "
                        "setting - so a clean driver install and stock clocks are the first "
                        "things to try.\n\n"
                        "If it only happens in windowed or borderless games, turning off "
                        "Multi-Plane Overlay is a known workaround for some driver and monitor "
                        "combinations. It is not a general fix for TDRs, and it needs a "
                        "restart to take effect.";
                    a.action = Action::DisableMPO;
                    a.needsReboot = true;
                    // NOT auto-safe: it cannot help the stall that suggested it,
                    // and a reboot-gated change applied unattended is one the
                    // user never watched happen.
                    a.autoSafe = false;
                    break;

                case Attribution::CauseKind::DiskReset:
                case Attribution::CauseKind::DiskRetry:
                    a.headline = "Check the drive - this is a hardware timeout, not a setting";
                    a.detail =
                        "Windows had to retry or reset a disk operation. That means the drive "
                        "did not answer in time: a failing disk, a loose or faulty cable, or a "
                        "storage driver problem.\n\n"
                        "Check the drive's SMART health, reseat its cable if it is an internal "
                        "SATA disk, and update the storage controller driver. ⚠️ Nothing "
                        "OptiMax can toggle fixes this, and if it is a failing drive, back it "
                        "up before anything else.";
                    // Deliberately no action - see the header note about not
                    // conflating a timeout with contention.
                    break;

                case Attribution::CauseKind::PowerTransition:
                    a.headline = "Stop Windows dropping the CPU into a low-power state";
                    a.detail =
                        "The system changed power state during the stall. Keeping all cores "
                        "awake and pinning the energy performance preference to maximum stops "
                        "a core being asleep when a frame needs it.\n\n"
                        "Costs a little idle power, applies immediately, and reverses cleanly.";
                    a.action = Action::KeepCoresAwake;
                    a.autoSafe = true;   // immediate, reversible, no security effect
                    break;
                }
                return a;
            }

            // For a stall nothing could be pinned on. Kept separate from
            // ForCause because "we looked and found nothing" and "we could not
            // look" are different statements and must not share wording - the
            // same distinction Attribution::Describe already makes.
            inline Advice ForUnattributed(bool allProbesRan) {
                Advice a;
                if (!allProbesRan) {
                    a.headline = "One of the system logs could not be read";
                    a.detail =
                        "OptiMax could not query a Windows event log, so this stall has not "
                        "been explained - not because nothing was found, but because the "
                        "lookup failed. Running as administrator usually fixes it.";
                    return a;
                }
                a.headline = "No system cause found - try the Performance section";
                a.detail =
                    "The event logs show nothing that explains this one. That usually means "
                    "the cause was ordinary CPU or disk contention rather than a discrete "
                    "event, which leaves the general tweaks: core tweaks for timer and "
                    "scheduling, and auto-boost so they are applied whenever a game starts.\n\n"
                    "⚠️ This is a general suggestion, not a diagnosis. OptiMax did look and "
                    "did not find a cause.";
                a.action = Action::CoreTweaks;
                a.autoSafe = true;       // immediate, reversible, already the app's core feature
                return a;
            }

            // Label for the apply button. Kept beside the enum so a new Action
            // cannot be added without one.
            inline const char* ActionLabel(Action a) {
                switch (a) {
                case Action::CoreTweaks:     return "Turn on core tweaks";
                case Action::KeepCoresAwake: return "Keep all cores awake";
                case Action::AutoBoost:      return "Turn on auto-boost";
                case Action::DisableMPO:     return "Disable MPO (needs a restart)";
                case Action::None:           break;
                }
                return "";
            }

            // The name used in log lines, so an automatic application is
            // greppable afterwards.
            inline const char* ActionLogName(Action a) {
                switch (a) {
                case Action::CoreTweaks:     return "core tweaks";
                case Action::KeepCoresAwake: return "keep cores awake";
                case Action::AutoBoost:      return "auto-boost";
                case Action::DisableMPO:     return "MPO disable";
                case Action::None:           break;
                }
                return "";
            }

        }
    }
}
