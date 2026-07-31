#pragma once
#include <windows.h>
#include <realtimeapiset.h>
#include <string>
#include <atomic>
#include <vector>
#include <mutex>
#include <thread>
#include <functional>
#include "OptiMaxGlobals.h"
#include "OptiMaxAttribution.h"   // v4.3.0: names the cause instead of "unattributed"
// The session report carries the suggested fix, not just the cause - so it needs
// the mapping. Remedy depends only on Attribution, which is already above, so
// this adds no cycle.
#include "OptiMaxRemedy.h"

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

        // Namespace-scope so the UI thread can read it. The audit noted that
        // m_ticks was per-instance while the counters above were not, which was
        // consistent only while there was exactly one monitor; the on-screen
        // report needs the count from a different thread, so it belongs here with
        // the others rather than being reached for through the monitor object.
        inline std::atomic<unsigned long long> g_tickCount{ 0 };

        // Published by the telemetry loop each tick, read by the watchdog thread.
        // NOT a read of g_isGameRunning: that lives in OptiMaxApp.h and does not
        // exist at this point in the include order, and reaching down from a lower
        // header for app-level state is the dependency that has broken this build
        // twice. StallMonitor solves the same problem with a parameter; the
        // watchdog cannot take one, because nothing calls into its loop - so it
        // gets a value pushed to it instead.
        inline std::atomic<bool> g_watchdogGameRunning{ false };

        // Bumped by the watchdog on every detected stall. The telemetry loop
        // compares it against the value it last saw to answer "has the system
        // stalled since my previous tick" for the heavy-work gate, without
        // sharing the watchdog's internals or duplicating its clock. Declared
        // here rather than beside the Watchdog class because StallMonitor - which
        // is defined earlier in this file - reads it.
        inline std::atomic<unsigned long long> g_stallSeq{ 0 };

        // 🔴 THE WATCHDOG'S PROOF OF LIFE, and it exists because the first version
        // of this class did not have it.
        //
        // A watchdog that reports only when something is wrong is unfalsifiable
        // when nothing is: a thread that never started, or died silently, produces
        // exactly the same empty log as a healthy quiet machine. That is the
        // ambiguity this entire component was written to remove - the comment at
        // the top of this file makes the argument, and StallMonitor answers it with
        // a summary line that is always printed. The watchdog was shipped without
        // an equivalent and its first live test was therefore uninterpretable: no
        // stall line, and no way to tell whether that was good news or a dead
        // thread.
        //
        // Every loop iteration bumps this, so "we were watching" is a number
        // anyone can read rather than something to be taken on trust.
        inline std::atomic<unsigned long long> g_watchdogChecks{ 0 };

        // One entry per reported stall, for the on-screen report. Kept as
        // structured data rather than re-parsed from the log because the log is
        // a fixed-size rolling window - by the time a user goes looking, the
        // lines describing their stall may already have been pushed out by the
        // ordinary chatter that followed it.
        struct StallRecord {
            ULONGLONG whenUtc = 0;      // wall clock, so it can be shown as a time
            ULONGLONG lateMs = 0;
            bool gameRunning = false;
            std::string cause;          // already phrased by Attribution::Describe

            // The classification behind that sentence, kept so the remedy layer
            // can map it to a suggestion without re-parsing the prose. Deriving
            // the kind by matching on the phrasing would tie the two together in
            // the worst way: rewording a message would silently change which
            // advice appears.
            Attribution::CauseKind kind{};
            bool attributed = false;    // a cause was found at all
            bool allProbesRan = false;  // and every log could actually be read
            bool rateLimited = false;   // skipped by the cooldown - not a finding
        };

        // Written on the telemetry thread, read on the UI thread, so it needs the
        // lock. Bounded by the same cap as the log detail lines: an unbounded
        // vector on a machine stalling every second is a slow leak, and a report
        // nobody can read is no more useful than no report.
        inline std::mutex g_recordMutex;
        inline std::vector<StallRecord> g_stallRecords;

        inline void RecordStall(const StallRecord& r) {
            std::lock_guard<std::mutex> lock(g_recordMutex);
            if (g_stallRecords.size() < (size_t)kMaxDetailLines) g_stallRecords.push_back(r);
        }

        inline std::vector<StallRecord> SnapshotStalls() {
            std::lock_guard<std::mutex> lock(g_recordMutex);
            return g_stallRecords;
        }

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
            // What the tick turned out to be, so the caller can feed the
            // self-governor without re-deriving any of it. The previous
            // signature returned void and the loop had no way to know a stall
            // had just happened, which is the fact most worth acting on.
            struct TickResult {
                bool stalled = false;
                bool ownWorkSlow = false;
                ULONGLONG lateMs = 0;
                ULONGLONG workMs = 0;
            };

            void BeginTick() {
                m_tickStartMs = UnbiasedNowMs();
            }

            TickResult EndTickAndSleep(DWORD sleepMs, bool gameRunning) {
                TickResult result;
                const ULONGLONG workEndMs = UnbiasedNowMs();
                const ULONGLONG workMs = (workEndMs > m_tickStartMs) ? (workEndMs - m_tickStartMs) : 0;

                // Wall clock either side of the sleep, captured because the
                // event log stamps records with wall-clock time and unbiased
                // time cannot be converted to it - that gap is the whole reason
                // the unbiased clock is used, so the two have to be recorded
                // separately. Cheap: GetSystemTimeAsFileTime is a read of a
                // value the kernel already maintains, not a syscall.
                const ULONGLONG wallBeforeSleep = Attribution::NowUtc();

                Sleep(sleepMs);

                const ULONGLONG wallAfterWake = Attribution::NowUtc();
                const ULONGLONG wakeMs = UnbiasedNowMs();
                const ULONGLONG sleptMs = (wakeMs > workEndMs) ? (wakeMs - workEndMs) : 0;
                const ULONGLONG lateMs = (sleptMs > sleepMs) ? (sleptMs - sleepMs) : 0;

                m_ticks++;
                g_tickCount++;

                // Time this thread was ready to run and did not get scheduled.
                // Nothing OptiMax did can account for it - the process was
                // blocked in Sleep for the whole window - so it is a genuine
                // system-wide observation and the one worth reporting.
                // v4.3.0: stall detection MOVED to Diagnostics::Watchdog. It was
                // measured here in 4.2.1 because this loop was the only thing that
                // woke on a timer, but this thread does real work, and from inside
                // the process a system-wide stall and a long tick body are
                // indistinguishable - so a detector living on a working thread is
                // confounded by construction. Leaving it here as well would also
                // double-count every stall into g_stallCount and the session
                // report, since both would increment it.
                //
                // The loop still needs to know whether the system stalled recently,
                // for the heavy-work gate. It reads that from the watchdog's
                // sequence number rather than measuring anything itself.
                {
                    const unsigned long long seq = g_stallSeq.load();
                    result.stalled = (seq != m_lastSeenStallSeq);
                    m_lastSeenStallSeq = seq;
                }

                // Publish for the watchdog, which cannot see g_isGameRunning.
                g_watchdogGameRunning = gameRunning;
                (void)wallBeforeSleep; (void)wallAfterWake;

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
                    result.ownWorkSlow = true;
                    Report("DIAGNOSTICS: OptiMax's own background check took "
                        + std::to_string(workMs) + "ms this tick (a few milliseconds is normal).");
                }

                result.lateMs = lateMs;
                result.workMs = workMs;
                return result;
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

                // The watchdog's count comes first for the same reason it does in
                // the on-screen report: zero invalidates the rest of the line.
                const unsigned long long wdChecks = g_watchdogChecks.load();
                if (wdChecks == 0) {
                    SafeAddLog("DIAGNOSTICS WARNING: Session summary - the stall watchdog did not run, "
                        "so nothing can be said about stalls this session. OptiMax's own checks: "
                        + checks + ".");
                    return;
                }

                if (stalls == 0 && slow == 0) {
                    SafeAddLog("DIAGNOSTICS: Session summary - no stalls detected across "
                        + std::to_string(wdChecks) + " stall checks and " + checks + ".");
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

            unsigned long long Ticks() const { return m_ticks; }

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
            unsigned long long m_lastSeenStallSeq = 0;
        };

        // ==============================================================
        // THE DEDICATED WATCHDOG (v4.3.0 part 1)
        //
        // WHY A SECOND THREAD, when StallMonitor already measures overshoot.
        // Because StallMonitor rides the telemetry loop, and that loop does real
        // work - process enumeration, RAM sampling, registry reads. From inside
        // the process a system-wide stall and "our own tick body ran long" look
        // identical, so anything measured on a working thread is confounded by
        // definition. This thread does nothing but sleep and check a clock, so
        // any overshoot it sees is genuinely the scheduler failing to run it.
        //
        // It also measures ten times as often. A 1Hz sample can only say the
        // stall happened somewhere in a one-second window, which is a poor
        // window to hand to attribution; 100ms narrows it by an order of
        // magnitude, and a narrower window is a more specific question to ask
        // the event log.
        //
        // COST. Ten wakes a second doing a clock read and a comparison. That is
        // genuinely negligible in CPU terms, and the deeper-C-state objection
        // does not apply while OptiMax is doing its job anyway - it already
        // holds a 1ms system timer for the whole of any game session.
        //
        // This thread is now the ONLY thing that reports stalls. StallMonitor's
        // stall branch is gone, because two detectors incrementing the same
        // counter would double-count every event into the session report.
        // ==============================================================
        inline constexpr DWORD kWatchdogIntervalMs = 100;

        // ==============================================================
        // 🔴 ATTRIBUTION IS RATE-LIMITED, AND THE REASON IS A DETECTION
        // GAP, NOT CPU COST.
        //
        // AttributeWindow queries the Windows event log. It costs a
        // measured 35ms on an idle machine - negligible - but during the
        // 2026-07-30 acceptance test the same query took about EIGHT
        // SECONDS against 20 high-priority spinners.
        //
        // It runs inline in the watchdog loop, between sleeps. While it
        // runs, the thread is not in Sleep, so it IS NOT MEASURING: any
        // stall occurring during attribution is never seen. That gap is
        // worst exactly when stalls are most likely, and it grew with the
        // number of stalls, because every one triggered a fresh query.
        // The 20-line log cap limited the OUTPUT, never the WORK.
        //
        // So: at most one query per cooldown. The first stall in each
        // window gets a real answer; the rest are recorded with their
        // duration and honestly marked as not checked. Five seconds is
        // chosen so a burst of stalls costs one query rather than N,
        // while a genuinely separate incident a few seconds later still
        // gets looked at properly.
        //
        // ⚠️ THIS REDUCES THE GAP, IT DOES NOT CLOSE IT. Closing it means
        // moving attribution off this thread onto a worker fed by a
        // queue, so the loop never stops sleeping. That is the right
        // long-term shape and is deliberately not done here - it is a
        // threading change, and this is a bounded fix to a measured
        // problem.
        // ==============================================================
        inline constexpr ULONGLONG kAttributionCooldownMs = 5000;

        class Watchdog {
        public:
            void Start() {
                if (m_thread.joinable()) return;   // idempotent
                m_run = true;
                m_thread = std::thread([this]() {
                    // Guarded like every other worker here: an exception reaching
                    // a thread entry point calls std::terminate, and killing the
                    // process to report a stall would be an outstanding own goal.
                    try { Loop(); }
                    catch (...) {
                        SafeAddLog("DIAGNOSTICS WARNING: The stall watchdog stopped unexpectedly. "
                            "Stall detection is off for the rest of this session.");
                    }
                    });
            }

            void Stop() {
                m_run = false;
                if (m_thread.joinable()) m_thread.join();
            }

            ~Watchdog() { Stop(); }

        private:
            void Loop() {
                while (m_run) {
                    // THE FIX: this measured elapsed time from the END of the
                    // PREVIOUS iteration, so everything the loop did after
                    // detecting a stall - AttributeWindow, which queries the event
                    // log, plus SafeAddLog - was counted into the NEXT interval and
                    // reported as the system holding us off.
                    //
                    // Idle that error is ~35ms and invisible under a 250ms
                    // threshold. Under load it is not: during the 2026-07-30
                    // acceptance test a query that costs 35ms idle took about eight
                    // seconds against 20 high-priority spinners, and those eight
                    // seconds were duly reported as a 7231ms system stall. The
                    // report's own timestamps gave it away - a 295ms stall recorded
                    // at 01:35:28 whose log line landed at 01:35:36, and the next
                    // "stall" beginning at exactly 01:35:36.
                    //
                    // This is the same confounding that stall detection was moved
                    // OFF the telemetry thread to escape, reintroduced one level
                    // down. Measuring only the Sleep excludes every cost this loop
                    // has, including its own, which is the whole point of the thread
                    // existing.
                    const ULONGLONG wallBefore = Attribution::NowUtc();
                    const ULONGLONG before = UnbiasedNowMs();
                    Sleep(kWatchdogIntervalMs);
                    const ULONGLONG after = UnbiasedNowMs();
                    g_watchdogChecks++;   // proof of life - see the declaration

                    const ULONGLONG slept = (after > before) ? (after - before) : 0;
                    const ULONGLONG late = (slept > kWatchdogIntervalMs)
                        ? (slept - kWatchdogIntervalMs) : 0;
                    if (late < kStallThresholdMs) continue;

                    // Checked AFTER the threshold test so a shutdown that happens
                    // to overrun cannot be reported as a stall - Stop() joins, and
                    // the last sleep before the flag clears would otherwise look
                    // like the system held us off.
                    if (!m_run) break;

                    g_stallCount++;
                    RecordWorstStall(late);
                    g_stallSeq++;

                    const ULONGLONG stallEndUtc = Attribution::NowUtc();

                    // Cooldown measured from the END of the last query, not its
                    // start. Under load the query itself is the expensive part,
                    // and timing from the start would let a slow query be
                    // followed immediately by another one - which is the case
                    // the limit exists for.
                    const ULONGLONG sinceLast = (m_haveLastReport
                        && stallEndUtc > m_lastAttributeUtc)
                        ? (stallEndUtc - m_lastAttributeUtc) / 10000ULL
                        : ~0ULL;

                    std::string cause;
                    bool freshQuery = false;
                    if (!m_haveLastReport || sinceLast >= kAttributionCooldownMs) {
                        m_lastReport = Attribution::AttributeWindow(wallBefore, stallEndUtc);
                        m_lastAttributeUtc = Attribution::NowUtc();
                        m_haveLastReport = true;
                        freshQuery = true;
                        cause = Attribution::Describe(m_lastReport);
                    }
                    else {
                        // Deliberately NOT the previous cause restated - see
                        // DescribeRateLimited. The stall is still counted, still
                        // timed, and still listed; only the lookup is skipped.
                        cause = Attribution::DescribeRateLimited(
                            m_lastReport, true, sinceLast);
                    }

                    StallRecord rec;
                    rec.whenUtc = wallBefore;
                    rec.lateMs = late;
                    rec.gameRunning = g_watchdogGameRunning.load();
                    rec.cause = cause;
                    // Only a FRESH query says anything about THIS stall. On a
                    // rate-limited one the record carries no classification at
                    // all rather than the previous stall's - the remedy layer
                    // must not advise on a lookup that did not happen, which is
                    // the same reason the cause string does not restate it.
                    rec.rateLimited = !freshQuery;
                    if (freshQuery) {
                        rec.attributed = m_lastReport.Attributed();
                        rec.allProbesRan = m_lastReport.AllProbesRan();
                        if (rec.attributed) rec.kind = m_lastReport.candidates.front().kind;
                    }
                    RecordStall(rec);

                    if (m_detailLines < kMaxDetailLines) {
                        SafeAddLog("DIAGNOSTICS: Stall detected - the system held OptiMax off for "
                            + std::to_string(late) + "ms"
                            + (rec.gameRunning ? " while a game was running" : "")
                            + ". Cause: " + cause + ".");
                        m_detailLines++;
                        if (m_detailLines == kMaxDetailLines) {
                            SafeAddLog("DIAGNOSTICS: Further stalls this session will be counted but not "
                                "listed individually. The totals appear when OptiMax closes.");
                        }
                    }
                }
            }

            std::thread m_thread;
            std::atomic<bool> m_run{ false };

            // Touched only from the watchdog thread, so no synchronisation is
            // needed - and none is added, because adding a mutex here would
            // imply a second writer that does not exist and invite one.
            Attribution::Report m_lastReport;
            ULONGLONG m_lastAttributeUtc = 0;
            bool m_haveLastReport = false;
            int m_detailLines = 0;
        };

        // ==============================================================
        // THE SESSION REPORT
        //
        // Built as one plain-text block rather than assembled in the UI, for the
        // reason the version string taught this project: two renderings of the
        // same facts drift apart. What is on screen and what lands on the
        // clipboard are the same string.
        //
        // Plain text on purpose. This is meant to be pasted into a Discord
        // message or a GitHub issue by someone asking for help - the whole gap
        // the rolling log was built to close was that a user reporting a problem
        // had nothing to send.
        // ==============================================================
        inline std::string FormatLocalTime(ULONGLONG utc) {
            FILETIME ft; ULARGE_INTEGER u; u.QuadPart = utc;
            ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
            FILETIME lft{}; SYSTEMTIME st{};
            if (!FileTimeToLocalFileTime(&ft, &lft) || !FileTimeToSystemTime(&lft, &st)) {
                return "unknown time";
            }
            char buf[16];
            sprintf_s(buf, "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
            return buf;
        }

        // Context the report needs and this header cannot reach. The version and
        // the tweak flags live in OptiMaxApp.h, above this in the include order -
        // the dependency that has broken this build twice. The app fills this in;
        // an empty one still produces a valid report, just a thinner one.
        struct ReportContext {
            std::string version;   // e.g. "4.3.0"
            std::string os;        // e.g. "Windows 10 21H2 (build 19044.7548)"
            // Answers "is the suggested tweak already on?". Supplied by the app
            // because only it can see the flags. Absent means the report omits
            // the applied/not-applied note rather than guessing at it.
            std::function<bool(Remedy::Action)> isAlreadyOn;
        };

        inline std::string BuildSessionReport(const ReportContext& ctx = ReportContext{}) {
            const unsigned long long ticks = g_tickCount.load();
            const int stalls = g_stallCount.load();
            const int slow = g_slowWorkCount.load();
            const auto records = SnapshotStalls();

            const unsigned long long wdChecks = g_watchdogChecks.load();

            std::string s = "OptiMax session diagnostics\n";
            if (!ctx.version.empty() || !ctx.os.empty()) {
                // Version and OS first, because the first thing anyone reading a
                // support report has to establish is what they are looking at.
                // Their absence cost real time on the 07-28 investigation.
                s += "OptiMax v" + (ctx.version.empty() ? std::string("?") : ctx.version);
                if (!ctx.os.empty()) s += "  -  " + ctx.os;
                s += "\n";
            }
            // 🔴 LABELLED, NOT JUST COUNTED. This read "Checks run: 89 (stall
            // watchdog: 913)", which tells a reader nothing about what either
            // number is or why they differ by an order of magnitude - and the
            // watchdog figure is the proof-of-life that decides whether any
            // verdict below is worth anything. It leads now, and says what it is.
            s += "Stall watchdog: " + std::to_string(wdChecks)
                + " checks at 10/sec (this is what detects stalls)\n";
            s += "Telemetry: " + std::to_string(ticks)
                + " samples at 1/sec (CPU, memory, latency)\n";

            // Stated before any verdict, because a zero here invalidates every
            // line below it. "No stalls detected" from a watchdog that never ran
            // is not a finding, and a support report that cannot be told apart
            // from a working one is worse than no report.
            if (wdChecks == 0) {
                s += "WARNING: the stall watchdog did not run this session, so no "
                    "conclusion below can be drawn about stalls.\n";
            }

            if (stalls == 0) {
                // Stated positively and always. "No stalls detected" is the line
                // whose absence made the 07-28 freeze report cost an hour -
                // silence is ambiguous between "nothing happened" and "nothing
                // was watching", and only one of those is reassuring.
                s += "Stalls: none detected.\n";
            }
            else {
                s += "Stalls: " + std::to_string(stalls)
                    + ", worst " + std::to_string(g_worstStallMs.load()) + "ms\n";
                for (const auto& r : records) {
                    s += "  " + FormatLocalTime(r.whenUtc) + "  held off "
                        + std::to_string(r.lateMs) + "ms";
                    if (r.gameRunning) s += " (game running)";
                    s += " - " + r.cause + "\n";

                    // 🔴 THE ADVICE GOES IN THE REPORT TOO.
                    //
                    // It was on screen only, which is precisely backwards: the
                    // report is the artefact a user sends when they cannot solve
                    // it themselves, so the one place the suggested fix most needs
                    // to appear is the one place it did not. A support reader was
                    // getting the diagnosis and none of the treatment.
                    //
                    // Nothing is emitted for a rate-limited stall - no lookup ran,
                    // so there is no advice, and inventing one here would undo the
                    // restraint the cause string already exercises.
                    if (!r.rateLimited) {
                        const Remedy::Advice adv = r.attributed
                            ? Remedy::ForCause(r.kind)
                            : Remedy::ForUnattributed(r.allProbesRan);
                        s += "      -> " + std::string(adv.headline) + "\n";
                        if (adv.Actionable()) {
                            s += "         suggested: "
                                + std::string(Remedy::ActionLogName(adv.action));
                            // Whether it is already on matters more than the
                            // suggestion itself: "try core tweaks" is noise to
                            // someone who has had them on all along, and a support
                            // reader cannot tell without being told.
                            if (ctx.isAlreadyOn) {
                                s += ctx.isAlreadyOn(adv.action)
                                    ? "  [already on]" : "  [not applied]";
                            }
                            if (adv.needsReboot) s += "  [needs a restart]";
                            s += "\n";
                        }
                    }
                }
                if ((int)records.size() < stalls) {
                    s += "  ...and " + std::to_string(stalls - (int)records.size())
                        + " more, not listed individually.\n";
                }
            }

            if (slow > 0) {
                // Reported separately and never folded into the stall count.
                // "Did OptiMax do something in that window" is the first question
                // any stall report raises and this is the honest answer to it.
                s += "OptiMax's own checks ran long: " + std::to_string(slow)
                    + (slow == 1 ? " time" : " times")
                    // Spelled out, because "ran long" beside a stall list reads as
                    // another symptom. It is the opposite: it is this app owning
                    // its own cost so the stall figures above cannot be quietly
                    // inflated by it.
                    + " (OptiMax's own cost, not counted as a system stall)\n";
            }
            return s;
        }
    }
}
