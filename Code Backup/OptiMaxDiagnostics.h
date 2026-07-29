#pragma once
#include <windows.h>
#include <realtimeapiset.h>
#include <string>
#include <atomic>
#include "OptiMaxGlobals.h"

namespace OptiMax {

    // ======================================================================
    // SESSION DIAGNOSTICS - STALL DETECTION (A5-3)
    //
    // THE PROBLEM this exists to solve: a user reported their game freezing
    // 2-3 times over a two-hour session, and OptiMax's log recorded NOTHING
    // for the entire two hours. Answering "why did my game freeze" took an
    // hour of manual correlation across the Defender operational log, the
    // System log, Task Scheduler results and live process metrics. The answer
    // (a Defender scheduled scan - see the GameSession comment in
    // OptiMaxPower.h) was findable, but no user is going to do that work.
    // They will just leave.
    //
    // A rolling log that says nothing while the thing the user is complaining
    // about is happening is not a diagnostic tool. This closes the blind spot
    // for free: the telemetry worker already wakes once a second, so the gap
    // between one wake and the next is a system-wide responsiveness sample
    // that costs nothing to take.
    //
    // WHAT IT CAN AND CANNOT SAY. It detects that time went missing and it
    // separates OptiMax's own cost from everybody else's. It does NOT know
    // what consumed the missing time - that is v4.3.0's job (correlating
    // against the Windows event log, Defender scan start/stop, TDRs, disk
    // timeouts). Until then this reports "unattributed" rather than guessing.
    // An app that invents a cause is worse than one that admits ignorance,
    // and a wrong attribution here would send users chasing the wrong thing.
    //
    // WHY THE UNBIASED CLOCK. QueryUnbiasedInterruptTime excludes time the
    // system spends asleep or hibernating. That matters more than it sounds:
    // with any clock that counts suspend time, closing a laptop lid or
    // sleeping the PC overnight makes Sleep(1000) appear to take eight hours,
    // and the app would announce a catastrophic stall every single time the
    // user wakes their machine. A feature whose most reliable output is a
    // false alarm gets ignored, and then it is worse than not having it.
    // Resolution is the clock-interrupt period (~15.6ms, finer while the
    // low-latency timer is engaged), which is an order of magnitude below the
    // thresholds here.
    // ======================================================================
    namespace Diagnostics {

        // A tick that takes longer than this to come round is worth a line in
        // the log. Sleep(1000) with the default 15.6ms timer normally returns
        // within ~16ms of its request, so 250ms is far outside ordinary
        // scheduling jitter and will not fire on a healthy idle machine.
        inline constexpr ULONGLONG kStallThresholdMs = 250;

        // After this many detailed lines the monitor goes quiet and only
        // counts. A machine stalling every second would otherwise flood the
        // rolling log and push out the very history someone would send to
        // support - the log is a fixed-size window, so spam actively destroys
        // evidence rather than merely adding noise.
        inline constexpr int kMaxDetailLines = 20;

        // Exposed for v4.3.0's session report, which needs to render these
        // rather than parse them back out of the log text.
        inline std::atomic<int> g_stallCount{ 0 };
        inline std::atomic<int> g_slowWorkCount{ 0 };
        inline std::atomic<unsigned long long> g_worstStallMs{ 0 };

        // "if (ms > worst) worst = ms" reads correct and is not. It is a
        // check-then-set: two threads can both pass the test and the smaller
        // one can land last, so the recorded worst stall is quietly wrong.
        // Harmless today - one monitor, one thread - but v4.3.0's whole plan is
        // a DEDICATED watchdog thread running alongside this one, and at that
        // point the bug is live and produces a plausible-looking wrong number,
        // which is the worst kind. Cheap to get right now, expensive to find
        // later. compare_exchange_weak reloads `prev` on failure, so the loop
        // both retries and re-tests.
        inline void RecordWorstStall(unsigned long long ms) {
            unsigned long long prev = g_worstStallMs.load(std::memory_order_relaxed);
            while (ms > prev &&
                   !g_worstStallMs.compare_exchange_weak(prev, ms, std::memory_order_relaxed)) {
            }
        }

        inline ULONGLONG UnbiasedNowMs() {
            ULONGLONG t = 0;
            if (!QueryUnbiasedInterruptTime(&t)) return 0;
            return t / 10000ULL;   // 100ns units -> ms
        }

        // Wraps the telemetry loop's own cadence. The monitor owns the sleep
        // deliberately: only the thing that decided how long to sleep can say
        // how much longer than that it actually took, and threading that
        // through as a separate "expected" parameter is how the two drift
        // apart later.
        //
        // Usage, once per iteration:
        //     mon.BeginTick();
        //     ...the work...
        //     mon.EndTickAndSleep(1000, gameRunning);
        // and once after the loop:
        //     mon.LogSessionSummary();
        //
        // gameRunning is passed in rather than read from g_isGameRunning,
        // which lives in OptiMaxApp.h and so does not exist yet at the point
        // this header is included. Reaching down into app-level state from a
        // lower header is the include-order dependency that has broken this
        // build twice; a parameter costs nothing and cannot.
        class StallMonitor {
        public:
            void BeginTick() {
                m_tickStartMs = UnbiasedNowMs();
            }

            void EndTickAndSleep(DWORD sleepMs, bool gameRunning) {
                const ULONGLONG workEndMs = UnbiasedNowMs();
                const ULONGLONG workMs = (workEndMs > m_tickStartMs) ? (workEndMs - m_tickStartMs) : 0;

                Sleep(sleepMs);

                const ULONGLONG wakeMs = UnbiasedNowMs();
                const ULONGLONG sleptMs = (wakeMs > workEndMs) ? (wakeMs - workEndMs) : 0;
                const ULONGLONG lateMs = (sleptMs > sleepMs) ? (sleptMs - sleepMs) : 0;

                m_ticks++;

                // Time this thread was ready to run and did not get scheduled.
                // Nothing OptiMax did can account for it - the process was
                // blocked in Sleep for the whole window - so it is a genuine
                // system-wide observation and the one worth reporting.
                if (lateMs >= kStallThresholdMs) {
                    g_stallCount++;
                    RecordWorstStall(lateMs);
                    Report("DIAGNOSTICS: Stall detected - the background monitor was held off for "
                        + std::to_string(lateMs) + "ms longer than it asked to wait"
                        + (gameRunning ? ", while a game was running" : "")
                        + ". OptiMax's own work in that tick was " + std::to_string(workMs)
                        + "ms, so the cause is outside OptiMax and is not yet attributed.");
                }

                // OptiMax's own cost. Usually a few milliseconds; a RAM trim or
                // a full process enumeration can legitimately spend longer, and
                // saying so plainly is the point - "did OptiMax do something in
                // that window" is the first question a stall report raises, and
                // this is the honest answer to it either way.
                //
                // A system-wide stall that happens to overlap the body lands
                // here too, since both look identical from inside the process.
                // The body is short, so the great majority of any stall falls
                // in the sleep above and is reported there.
                if (workMs >= kStallThresholdMs) {
                    g_slowWorkCount++;
                    Report("DIAGNOSTICS: OptiMax's own background check took "
                        + std::to_string(workMs) + "ms this tick (a few milliseconds is normal).");
                }
            }

            // Always logged, including - especially - when nothing went wrong.
            // "No stalls detected over 7200 checks" is precisely the line whose
            // absence made the original freeze report cost an hour: silence is
            // ambiguous between "nothing happened" and "nothing was watching".
            void LogSessionSummary() const {
                const int stalls = g_stallCount.load();
                const int slow = g_slowWorkCount.load();
                const std::string checks = std::to_string(m_ticks)
                    + (m_ticks == 1 ? " background check" : " background checks");

                if (stalls == 0 && slow == 0) {
                    SafeAddLog("DIAGNOSTICS: Session summary - no stalls detected across " + checks + ".");
                    return;
                }

                std::string msg = "DIAGNOSTICS: Session summary - " + std::to_string(stalls)
                    + (stalls == 1 ? " stall across " : " stalls across ") + checks;
                if (stalls > 0) {
                    msg += ", worst " + std::to_string(g_worstStallMs.load()) + "ms";
                }
                if (slow > 0) {
                    msg += "; OptiMax's own work ran long " + std::to_string(slow)
                        + (slow == 1 ? " time" : " times");
                }
                SafeAddLog(msg + ".");
            }

        private:
            void Report(const std::string& msg) {
                if (m_detailLines < kMaxDetailLines) {
                    SafeAddLog(msg);
                    m_detailLines++;
                    if (m_detailLines == kMaxDetailLines) {
                        // "findings", not "stalls" - this cap covers the
                        // slow-own-work lines too, and the harness run that
                        // tripped it was entirely slow-work.
                        SafeAddLog("DIAGNOSTICS: Further findings this session will be counted but not "
                            "listed individually, to keep the log readable. The totals appear when "
                            "OptiMax closes.");
                    }
                }
            }

            ULONGLONG m_tickStartMs = 0;
            unsigned long long m_ticks = 0;
            int m_detailLines = 0;
        };
    }
}
