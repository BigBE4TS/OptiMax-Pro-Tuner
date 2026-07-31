#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "OptiMaxAttribution.h"

// ==========================================================================
// SELF-GOVERNOR (v4.3.0)
//
// What OptiMax does with its own diagnostic data. The deliberate boundary:
// this decides what OPTIMAX does, never what WINDOWS does.
//
// WHY THAT BOUNDARY. The attribution data mostly names causes this project
// must not or cannot act on. A Defender scan is the most common attributed
// cause and reconfiguring Defender is prohibited outright - it is textbook
// malware behaviour and would plausibly re-trigger the ML detections the
// v4.2.0 clearance was won against. A TDR is a driver bug. A disk retry is
// failing hardware. None of them has a safe automatic remedy, so an
// auto-apply path built on this data would spend its time acting on
// inferences it cannot act on correctly.
//
// Worse, it changes what a wrong answer costs. Today a bad attribution prints
// a wrong sentence. With auto-apply it becomes an unrequested change to
// someone's system - and this codebase has six recorded revert paths that
// logged success and called nothing. A false positive driving an automatic
// change, with a revert that might not work, is the compounding version of
// every defect the last two sessions were spent removing.
//
// So the data is used the one way that cannot hurt anyone: OptiMax gets out
// of the way. Deferring its own work needs no new permission, has no revert
// to get wrong, and is strictly safer than reaching further into Windows.
//
// The evidence that this is worth doing at all is from OptiMax's own log:
// game detection spent 3030ms evicting working sets and dropping the file
// cache at the exact moment a game was loading. The tool was the contention.
// Nothing here is hypothetical.
// ==========================================================================

namespace OptiMax {
    namespace Diagnostics {
        namespace SelfGovernor {

            using Attribution::Report;
            using Attribution::CauseKind;
            using Attribution::ProbeStatus;

            // =============================================================
            // 1. ADAPTIVE CADENCE
            //
            // If OptiMax's own tick body keeps running long, waking up just as
            // often makes it a larger share of a machine that is already
            // struggling. Backing off is the honest response.
            //
            // Capped, and the cap matters more than the growth: the telemetry
            // tick is what notices a game starting and what samples RAM. A
            // gate that backs off without limit would quietly turn game
            // detection into a ten-second lag, trading a diagnostic nicety for
            // the app's primary function.
            // =============================================================
            inline constexpr DWORD kBaseTickMs = 1000;
            inline constexpr DWORD kMaxTickMs = 4000;

            inline DWORD AdaptiveTickMs(DWORD baseMs, int consecutiveSlowTicks) {
                if (consecutiveSlowTicks <= 0) return baseMs;
                // Linear, not exponential. Exponential reaches the cap in three
                // ticks, which in practice means "always capped" the moment
                // anything is briefly busy.
                const DWORD grown = baseMs + (DWORD)consecutiveSlowTicks * 500u;
                return (grown > kMaxTickMs) ? kMaxTickMs : grown;
            }

            // =============================================================
            // 2. THE HEAVY-WORK GATE
            //
            // Answers one question: is now a bad moment for OptiMax to start
            // something expensive (a RAM purge, a restore point, a full
            // process enumeration)?
            //
            // TWO TIERS, DELIBERATELY. NoteTick is free and is fed by the
            // StallMonitor data already collected. The event-log probe costs
            // ~35ms, which is nothing once before a purge on a 60-second
            // cooldown and would be 3.5% of a core if it ran every tick. The
            // split is the whole reason this is affordable.
            // =============================================================
            enum class Deferral {
                Proceed,
                DeferRecentStall,
                DeferOwnWorkSlow,
                DeferScanRunning
            };

            struct GateDecision {
                Deferral decision = Deferral::Proceed;
                std::string reason;          // log-ready, says what and why
                bool Proceeds() const { return decision == Deferral::Proceed; }
            };

            // How many recent ticks are considered. At a 1s cadence this is a
            // ten-second memory: long enough that a stall suppresses the work
            // that would have landed on top of it, short enough that the gate
            // reliably releases. A guard that never lets go is its own bug -
            // the MMCSS snap-back test exists because of exactly that.
            inline constexpr size_t kHistoryTicks = 10;

            class LoadGate {
            public:
                void NoteTick(bool stalled, bool ownWorkSlow) {
                    m_stalled[m_next] = stalled;
                    m_slow[m_next] = ownWorkSlow;
                    m_next = (m_next + 1) % kHistoryTicks;
                    if (m_filled < kHistoryTicks) m_filled++;

                    // Consecutive count drives the cadence, so it resets on the
                    // first healthy tick rather than decaying.
                    m_consecutiveSlow = ownWorkSlow ? m_consecutiveSlow + 1 : 0;
                }

                int ConsecutiveSlowTicks() const { return m_consecutiveSlow; }

                int RecentStalls() const {
                    int n = 0;
                    for (size_t i = 0; i < m_filled; ++i) if (m_stalled[i]) n++;
                    return n;
                }
                int RecentSlowTicks() const {
                    int n = 0;
                    for (size_t i = 0; i < m_filled; ++i) if (m_slow[i]) n++;
                    return n;
                }

                // probeSystem = true spends the ~35ms event log query. Pass
                // false from anywhere that runs often.
                GateDecision ShouldStartHeavyWork(bool probeSystem,
                    ULONGLONG nowUtc = 0) const {
                    GateDecision d;

                    if (RecentStalls() > 0) {
                        d.decision = Deferral::DeferRecentStall;
                        d.reason = "the system stalled within the last "
                            + std::to_string(kHistoryTicks)
                            + " checks, so this was put off rather than added on top of it";
                        return d;
                    }
                    if (RecentSlowTicks() >= 2) {
                        d.decision = Deferral::DeferOwnWorkSlow;
                        d.reason = "OptiMax's own checks have been running long, so this was "
                            "put off to stop it making that worse";
                        return d;
                    }
                    if (probeSystem) {
                        const ULONGLONG now = nowUtc ? nowUtc : Attribution::NowUtc();
                        Report r = Attribution::AttributeWindow(now, now, 0);
                        for (const auto& c : r.candidates) {
                            if (c.kind == CauseKind::DefenderScan) {
                                d.decision = Deferral::DeferScanRunning;
                                d.reason = "a Windows Defender scan is running, so this was put off "
                                    "until it finishes rather than competing with it for the disk";
                                return d;
                            }
                        }
                        // A probe that could not run is NOT a reason to defer.
                        // Failing closed here would mean a machine with a
                        // disabled Defender channel never purges RAM again, and
                        // would say nothing about why.
                    }
                    d.reason = "no stall or scan in the recent window";
                    return d;
                }

            private:
                bool m_stalled[kHistoryTicks]{};
                bool m_slow[kHistoryTicks]{};
                size_t m_next = 0;
                size_t m_filled = 0;
                int m_consecutiveSlow = 0;
            };

            // =============================================================
            // 3. MEASUREMENT CONTAMINATION
            //
            // The 60-second latency test presents its result as a description
            // of an idle system. If a Defender scan ran through it, the number
            // is not that - it is a measurement of a machine under load,
            // displayed as though it were a baseline. That is the same class as
            // the timer warning that reported safe mode as a failure: a
            // plausible wrong number, which is worse than no number, because
            // users quote these at each other.
            //
            // The test already refuses to run while a game is going and
            // abandons its samples if one starts mid-run. This is the same
            // rule applied to the other thing that invalidates a reading.
            // =============================================================
            enum class ReadingVerdict { Clean, Contaminated, Unknown };

            struct ReadingCheck {
                ReadingVerdict verdict = ReadingVerdict::Unknown;
                std::string reason;
                bool Publishable() const { return verdict == ReadingVerdict::Clean; }
            };

            // Only causes at or above this weight invalidate a reading. A
            // single retried I/O (40) is real and will show in the numbers, but
            // it is part of how this machine actually behaves - discarding a
            // whole minute of measurement for it would mean a busy disk could
            // never produce a reading at all. A scan (80), a device reset (60)
            // or a TDR (100) are different in kind: they describe a system that
            // is not the one the result claims to describe.
            inline constexpr int kContaminatingWeight = 60;

            // Pure, so it can be tested against a constructed Report including
            // the failure cases a live query will not reproduce on demand.
            inline ReadingCheck ClassifyReading(const Report& r) {
                ReadingCheck c;

                if (!r.AllProbesRan()) {
                    // NOT Clean. We could not look, and publishing a reading
                    // stamped "verified quiet" on the back of a probe that never
                    // ran is exactly the lie this project keeps finding.
                    c.verdict = ReadingVerdict::Unknown;
                    c.reason = "could not confirm the system was idle - one of the system logs "
                        "could not be read, so this result is shown without that check";
                    return c;
                }

                for (const auto& cand : r.candidates) {
                    if (Attribution::ExplanatoryWeight(cand.kind) >= kContaminatingWeight) {
                        c.verdict = ReadingVerdict::Contaminated;
                        c.reason = std::string("discarded - ")
                            + Attribution::KindPhrase(cand.kind)
                            + " during the test, so this would describe a loaded system rather "
                            "than the idle one the result is presented as";
                        return c;
                    }
                }

                if (!r.candidates.empty()) {
                    c.verdict = ReadingVerdict::Clean;
                    c.reason = "usable, though minor disk activity was logged during the test";
                    return c;
                }
                c.verdict = ReadingVerdict::Clean;
                c.reason = "no interfering system activity was found during the test";
                return c;
            }

            inline ReadingCheck CheckMeasurementWindow(ULONGLONG startUtc, ULONGLONG endUtc) {
                // Zero slack: the question is what happened DURING the test, not
                // near it. A scan starting two seconds after the test ended did
                // not affect it, and widening the window would throw away good
                // readings.
                return ClassifyReading(Attribution::AttributeWindow(startUtc, endUtc, 0));
            }

        }
    }
}
