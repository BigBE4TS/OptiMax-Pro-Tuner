#pragma once
#include <windows.h>
#include <winevt.h>
#include <psapi.h>      // ProcessLoad's per-process CPU sampling
#include "OptiMaxText.h" // ToUtf8 - process names are paths and may not be ASCII
#include <string>
#include <vector>
#include <algorithm>
#include <mutex>

#pragma comment(lib, "wevtapi.lib")

// ==========================================================================
// SESSION DIAGNOSTICS - STALL ATTRIBUTION (v4.3.0)
//
// The other half of OptiMaxDiagnostics.h. StallMonitor can say "time went
// missing and it was not us"; this says what was running when it went.
//
// WHY THE EVENT LOG API AND NOT A COMMAND. wevtutil.exe and PowerShell's
// Get-WinEvent would both answer the same question in one line, and both are
// unavailable to this project: spawning a script interpreter is the single
// strongest AV heuristic this codebase organises itself to avoid, and
// Purger::DeepClean's four cmd.exe spawns were removed for exactly that
// reason. EvtQuery is the documented API, needs no child process, and is what
// the Event Viewer itself uses.
//
// The legacy ReadEventLog cannot do this job at all - it only reaches the
// classic Application/System/Security logs, and the Defender channel
// (Microsoft-Windows-Windows Defender/Operational) is a modern channel that
// does not appear there. wevtapi is not a preference, it is the only route.
//
// WHY WALL-CLOCK TIME IS PASSED IN. StallMonitor measures with
// QueryUnbiasedInterruptTime, deliberately, so that sleeping the machine does
// not read as an eight-hour stall. But unbiased time has no relationship to
// the wall clock the event log stamps its records with, and none can be
// derived - that gap IS the feature. So the caller must capture
// GetSystemTimeAsFileTime at the moment it notices the stall and hand both
// values over. Trying to reconstruct the wall clock from unbiased time here
// would produce a window that drifts by however long the machine has been
// asleep this boot, and would attribute stalls to scans that happened hours
// away.
//
// THE DISTINCTION THIS FILE EXISTS TO PRESERVE: "we looked and found nothing"
// and "we could not look" are different answers, and collapsing them is this
// project's documented recurring defect. A missing channel, a permissions
// failure or a malformed query must never surface as "unattributed" - that
// word is a finding, and it has to keep meaning one.
//
// WHY SCANS ARE MODELLED AS INTERVALS. The first version of this file matched
// a stall against event TIMESTAMPS, and it found the 2026-07-28 incident only
// by luck: the stall coincided with the scan's start event. That scan ran
// 23:13:26 -> 23:13:59, so a stall at 23:13:40 sits BETWEEN the two events
// and a timestamp match finds nothing at all - while the scan is very much
// still running. Since a stall is far more likely to land in the middle of a
// 33-second scan than on its first millisecond, the timestamp model would
// have missed the ordinary case and caught only the rare one. Defender events
// are therefore paired into intervals and the question asked is "was a scan in
// progress", which is the question that was always meant.
// ==========================================================================

namespace OptiMax {
    namespace Diagnostics {
        namespace Attribution {

            // EvtClose for every handle type the API hands back. Non-copyable
            // for the same reason ScopedHandle in OptiMaxEngine.h is: copying
            // one would double-close.
            class ScopedEvt {
            public:
                explicit ScopedEvt(EVT_HANDLE h = NULL) : m_h(h) {}
                ~ScopedEvt() { if (m_h) EvtClose(m_h); }
                ScopedEvt(const ScopedEvt&) = delete;
                ScopedEvt& operator=(const ScopedEvt&) = delete;
                operator EVT_HANDLE() const { return m_h; }
                EVT_HANDLE get() const { return m_h; }
                bool valid() const { return m_h != NULL; }
            private:
                EVT_HANDLE m_h;
            };

            enum class CauseKind {
                OptiMaxItself,      // WE did it - see the SelfActivity ledger below
                ProcessContention,  // the CPU was oversubscribed - see ProcessLoad
                DisplayReset,       // System 4101 - the display driver reset
                DefenderScan,       // an interval during which a scan was running
                DiskReset,          // System 129 - reset to device issued
                DiskRetry,          // System 153 - an I/O was retried
                PowerTransition,    // Kernel-Power
                RawScanEvent        // an unpaired Defender record, pre-interval
            };

            struct Candidate {
                CauseKind kind{};
                ULONGLONG startUtc = 0;     // FILETIME as ULONGLONG, 100ns units
                ULONGLONG endUtc = 0;       // == startUtc for an instantaneous event
                bool endOpen = false;       // interval with no observed end yet
                bool startedBeforeLookback = false;
                DWORD eventId = 0;
                std::wstring channel;
                // Only ever set for OptiMaxItself, and it is a static literal
                // rather than a std::string for the reason given in the
                // SelfActivity ledger below.
                const char* selfWhat = nullptr;

                // Only ever set for ProcessContention: the pre-rendered
                // "97% - powershell.exe (24 processes, 52%)" breakdown.
                // Built once where the numbers live rather than in the UI,
                // for the same reason Describe() is in this file at all.
                std::string detail;

                bool Overlaps(ULONGLONG from, ULONGLONG to) const {
                    const ULONGLONG hi = endOpen ? ~0ULL : endUtc;
                    return startUtc <= to && hi >= from;
                }
                ULONGLONG DurationMs() const {
                    if (endOpen || endUtc <= startUtc) return 0;
                    return (endUtc - startUtc) / 10000ULL;
                }
            };

            // Three outcomes, not two. ProbedOk with an empty vector is a real
            // finding; the other two are admissions.
            enum class ProbeStatus {
                ProbedOk,
                ChannelUnavailable,   // channel absent, disabled, or access denied
                QueryFailed           // opened, then failed part way through
            };

            struct ProbeOutcome {
                ProbeStatus status = ProbeStatus::ProbedOk;
                DWORD winError = 0;
                std::wstring channel;
            };

            // ---------------------------------------------------------------
            // The channels and IDs v4.3.0 correlates against. Every one of
            // these was confirmed present and populated on the dev machine
            // before being listed, rather than copied from documentation:
            // Defender 1000/1001/1002 (2254 records in the channel), System
            // 153 (15 occurrences in 30 days), Kernel-Power transitions. 4101
            // is included and produced nothing here - a TDR is a real stutter
            // cause, this box simply has not had one.
            // ---------------------------------------------------------------
            inline const wchar_t* kDefenderChannel = L"Microsoft-Windows-Windows Defender/Operational";
            inline const wchar_t* kSystemChannel = L"System";

            inline constexpr DWORD kDefenderScanStarted = 1000;
            inline constexpr DWORD kDefenderScanCompleted = 1001;
            inline constexpr DWORD kDefenderScanStopped = 1002;

            // How far back to look for a scan that is still running. A quick
            // scan is over in under a minute; a full scan can run for hours.
            // Six hours covers a full scan without being unbounded, and the
            // query is bounded by event ID as well as time so the cost is set
            // by how many scans happened, not by how long the window is.
            inline constexpr ULONGLONG kScanLookbackMs = 6ULL * 60 * 60 * 1000;

            inline ULONGLONG FileTimeToU64(const FILETIME& ft) {
                ULARGE_INTEGER u;
                u.LowPart = ft.dwLowDateTime;
                u.HighPart = ft.dwHighDateTime;
                return u.QuadPart;
            }

            inline ULONGLONG NowUtc() {
                FILETIME ft;
                GetSystemTimeAsFileTime(&ft);
                return FileTimeToU64(ft);
            }

            // The event log's XPath dialect compares @SystemTime against an
            // ISO 8601 string in UTC. Millisecond precision is kept because
            // scan start and scan stop can land in the same second, and a
            // window that rounds them together cannot order them.
            inline std::wstring FormatIso8601Utc(ULONGLONG utc) {
                FILETIME ft;
                ULARGE_INTEGER u; u.QuadPart = utc;
                ft.dwLowDateTime = u.LowPart;
                ft.dwHighDateTime = u.HighPart;

                SYSTEMTIME st{};
                if (!FileTimeToSystemTime(&ft, &st)) return L"1601-01-01T00:00:00.000Z";

                wchar_t buf[64];
                swprintf_s(buf, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
                return buf;
            }

            inline std::wstring BuildQuery(const std::vector<DWORD>& ids,
                ULONGLONG startUtc, ULONGLONG endUtc) {
                std::wstring idClause;
                for (size_t i = 0; i < ids.size(); ++i) {
                    if (i) idClause += L" or ";
                    idClause += L"EventID=" + std::to_wstring(ids[i]);
                }
                // Bounded on both sides. An unbounded query on the System log
                // would walk ~9800 records to answer a question about a
                // two-second window, and this runs while the machine is
                // already struggling - that is the worst possible moment to be
                // wasteful.
                return L"*[System[(" + idClause + L") and TimeCreated["
                    L"@SystemTime>='" + FormatIso8601Utc(startUtc) + L"' and "
                    L"@SystemTime<='" + FormatIso8601Utc(endUtc) + L"']]]";
            }

            inline CauseKind ClassifySystemEvent(DWORD id) {
                if (id == 4101) return CauseKind::DisplayReset;
                if (id == 129)  return CauseKind::DiskReset;
                if (id == 153)  return CauseKind::DiskRetry;
                return CauseKind::PowerTransition;
            }

            // Queries one channel for one set of IDs inside one window.
            //
            // Returns the outcome; `out` is only meaningful when the status is
            // ProbedOk. Partial results from a QueryFailed run are deliberately
            // discarded rather than returned - a caller handed three of five
            // events with no way to know it had a truncated set would report a
            // confident wrong attribution, which is worse than reporting that
            // the probe broke.
            inline ProbeOutcome QueryChannel(const wchar_t* channel,
                const std::vector<DWORD>& ids,
                ULONGLONG startUtc,
                ULONGLONG endUtc,
                std::vector<Candidate>& out) {
                ProbeOutcome outcome;
                outcome.channel = channel;

                const std::wstring query = BuildQuery(ids, startUtc, endUtc);

                ScopedEvt results(EvtQuery(NULL, channel, query.c_str(),
                    EvtQueryChannelPath | EvtQueryReverseDirection));
                if (!results.valid()) {
                    // ERROR_EVT_CHANNEL_NOT_FOUND, ERROR_ACCESS_DENIED and a
                    // malformed query all land here. They are all "could not
                    // look", which is the only thing the caller needs to know
                    // to avoid claiming a finding it does not have.
                    outcome.status = ProbeStatus::ChannelUnavailable;
                    outcome.winError = GetLastError();
                    return outcome;
                }

                ScopedEvt ctx(EvtCreateRenderContext(0, NULL, EvtRenderContextSystem));
                if (!ctx.valid()) {
                    outcome.status = ProbeStatus::QueryFailed;
                    outcome.winError = GetLastError();
                    return outcome;
                }

                std::vector<Candidate> found;
                std::vector<BYTE> buf(4096);

                for (;;) {
                    EVT_HANDLE hEvent = NULL;
                    DWORD returned = 0;
                    // Zero timeout: this runs on a machine that is already
                    // stalling. Blocking here to wait for the event log
                    // service would add OptiMax's own delay to the very
                    // measurement it is trying to explain.
                    if (!EvtNext(results, 1, &hEvent, 0, 0, &returned)) {
                        const DWORD err = GetLastError();
                        if (err == ERROR_NO_MORE_ITEMS) break;
                        outcome.status = ProbeStatus::QueryFailed;
                        outcome.winError = err;
                        return outcome;
                    }
                    ScopedEvt ev(hEvent);

                    DWORD used = 0, propCount = 0;
                    if (!EvtRender(ctx, ev, EvtRenderEventValues,
                        (DWORD)buf.size(), buf.data(), &used, &propCount)) {
                        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                            buf.resize(used);
                            if (!EvtRender(ctx, ev, EvtRenderEventValues,
                                (DWORD)buf.size(), buf.data(), &used, &propCount)) {
                                outcome.status = ProbeStatus::QueryFailed;
                                outcome.winError = GetLastError();
                                return outcome;
                            }
                        }
                        else {
                            outcome.status = ProbeStatus::QueryFailed;
                            outcome.winError = GetLastError();
                            return outcome;
                        }
                    }

                    const EVT_VARIANT* vals = reinterpret_cast<const EVT_VARIANT*>(buf.data());

                    // Guard the property count before indexing. A rendered
                    // event is documented to carry the full system block, but
                    // indexing past a short array on a malformed record would
                    // read whatever follows the buffer and report it as an
                    // event ID.
                    if (propCount <= (DWORD)EvtSystemTimeCreated) continue;

                    Candidate c;
                    c.channel = channel;
                    c.eventId = (vals[EvtSystemEventID].Type == EvtVarTypeNull)
                        ? 0 : vals[EvtSystemEventID].UInt16Val;
                    c.startUtc = (vals[EvtSystemTimeCreated].Type == EvtVarTypeNull)
                        ? 0 : vals[EvtSystemTimeCreated].FileTimeVal;
                    c.endUtc = c.startUtc;
                    c.kind = (c.channel == kDefenderChannel)
                        ? CauseKind::RawScanEvent
                        : ClassifySystemEvent(c.eventId);
                    found.push_back(c);
                }

                // Reverse direction gives newest first; chronological reads
                // better in a report and the interval pass below requires it.
                std::sort(found.begin(), found.end(),
                    [](const Candidate& a, const Candidate& b) { return a.startUtc < b.startUtc; });

                out.insert(out.end(), found.begin(), found.end());
                return outcome;
            }

            // Pairs raw Defender records into scan intervals.
            //
            // Three cases, and the two asymmetric ones are the interesting
            // part - both are normal, not corrupt data:
            //
            //  start ... end     an ordinary completed scan.
            //  start ... <none>  the scan is STILL RUNNING. This is the live
            //                    case: at the instant a stall is detected the
            //                    end event does not exist yet, so an open
            //                    interval is what a real attribution looks
            //                    like, not an error.
            //  <none> ... end    the scan began BEFORE the lookback window.
            //                    Discarding this would silently lose exactly
            //                    the long full scans most likely to cause a
            //                    stall, so the interval is extended back to the
            //                    window start and flagged as clipped.
            inline std::vector<Candidate> BuildScanIntervals(
                const std::vector<Candidate>& rawEvents,
                ULONGLONG lookbackStartUtc) {
                std::vector<Candidate> intervals;
                bool open = false;
                Candidate cur{};

                for (const auto& e : rawEvents) {
                    if (e.kind != CauseKind::RawScanEvent) continue;

                    if (e.eventId == kDefenderScanStarted) {
                        // A second start with no intervening end means the end
                        // fell outside the queried IDs or the log rolled. Close
                        // the previous one open rather than dropping it.
                        if (open) {
                            cur.endOpen = true;
                            intervals.push_back(cur);
                        }
                        cur = Candidate{};
                        cur.kind = CauseKind::DefenderScan;
                        cur.channel = kDefenderChannel;
                        cur.eventId = e.eventId;
                        cur.startUtc = e.startUtc;
                        cur.endUtc = e.startUtc;
                        open = true;
                    }
                    else {
                        if (open) {
                            cur.endUtc = e.startUtc;
                            cur.endOpen = false;
                            intervals.push_back(cur);
                            open = false;
                        }
                        else {
                            Candidate clipped{};
                            clipped.kind = CauseKind::DefenderScan;
                            clipped.channel = kDefenderChannel;
                            clipped.eventId = e.eventId;
                            clipped.startUtc = lookbackStartUtc;
                            clipped.endUtc = e.startUtc;
                            clipped.startedBeforeLookback = true;
                            intervals.push_back(clipped);
                        }
                    }
                }
                if (open) {
                    cur.endOpen = true;
                    intervals.push_back(cur);
                }
                return intervals;
            }

            // ---------------------------------------------------------------
            // Ranking. Without this, Describe() named whichever candidate
            // happened to be earliest, so a single retried disk I/O would
            // outrank an active virus scan purely by arriving first - a
            // plausible-looking wrong answer, which is the failure mode this
            // whole feature is supposed to remove rather than add.
            //
            // The ordering is by how much of a multi-hundred-millisecond stall
            // the cause can actually account for:
            //   a display reset freezes the compositor outright;
            //   a running scan is sustained CPU and disk contention;
            //   a device reset is seconds of blocked I/O;
            //   a single retried I/O is real but small, and is as much a
            //     symptom of contention as a cause of it;
            //   a power transition is usually coincidental.
            // ---------------------------------------------------------------
            // ==========================================================
            // "DID OPTIMAX DO THIS?" - the first question any stall
            // report raises, and until now this file could not answer it.
            //
            // 🎯 THE ONE ATTRIBUTION SOURCE THAT IS KNOWLEDGE RATHER THAN
            // CORRELATION. Every other candidate here is inferred by
            // matching a stall window against a log somebody else wrote,
            // so a match is evidence. This one is recorded by the code
            // doing the work, at the moment it does it - a match is not
            // evidence that OptiMax might be responsible, it is a record
            // that OptiMax was busy for exactly that span. That is why it
            // outranks every Windows cause in ExplanatoryWeight below.
            //
            // 🔴 IT MATTERS MORE SINCE THE AUTOMATIC REMEDY SHIPPED.
            // Applying core tweaks takes a system restore point first,
            // which is slow and disk-heavy, and the remedy runs it
            // unattended. Without this ledger a stall OptiMax caused
            // would be reported to the user as "unattributed - no
            // matching system event was found" - the app blaming Windows
            // for its own work. Same defect class as a toggle reporting
            // success for a write it never made.
            //
            // Recorded as INTERVALS for the reason already established
            // for Defender scans at the top of this file: a restore point
            // runs for seconds, so a stall is far likelier to land in the
            // middle of one than on its first millisecond.
            // ==========================================================
            namespace SelfActivity {

                struct Record {
                    ULONGLONG startUtc = 0;
                    ULONGLONG endUtc = 0;
                    bool open = false;            // still running
                    const char* what = nullptr;
                    unsigned long long id = 0;
                };

                // Bounded for the same reason the stall detail lines are
                // capped at 20: the rolling log and this ring are both
                // fixed-size evidence, and unbounded growth destroys the
                // history somebody would send to support.
                inline constexpr size_t kCapacity = 32;

                inline std::mutex g_selfMutex;
                inline std::vector<Record> g_selfRecords;
                inline unsigned long long g_selfNextId = 1;

                // ⚠️ `what` MUST be a string literal with static storage.
                // This is called from the UI thread mid-frame and from the
                // telemetry thread, under a lock, on paths that run while
                // the machine is already struggling. A std::string would
                // allocate there, and allocating under a lock during a
                // stall is the tool making the thing it measures worse -
                // the same reasoning that gated the DPC probe behind
                // !g_isGameRunning.
                // ⚠️ Shared by Begin and by the test seam, so the harness
                // cannot exercise a ring that behaves differently from the
                // shipping one. It was NOT shared originally and the harness
                // caught it: RecordForTest grew without bound while Begin
                // evicted correctly, which is the "green against code nobody
                // ships" failure this project has already been bitten by.
                // Caller must hold g_selfMutex.
                inline void EvictIfFullLocked() {
                    if (g_selfRecords.size() < kCapacity) return;
                    // Drop the oldest CLOSED record in preference to an open
                    // one. An open record is work in progress right now, which
                    // is the single most likely explanation for a stall
                    // happening right now - dropping it to make room would
                    // discard the best evidence in the ring.
                    auto it = std::find_if(g_selfRecords.begin(), g_selfRecords.end(),
                        [](const Record& r) { return !r.open; });
                    g_selfRecords.erase(it != g_selfRecords.end() ? it : g_selfRecords.begin());
                }

                inline unsigned long long Begin(const char* what) {
                    if (!what) return 0;
                    std::lock_guard<std::mutex> lock(g_selfMutex);
                    EvictIfFullLocked();
                    Record r;
                    r.startUtc = NowUtc();
                    r.open = true;
                    r.what = what;
                    r.id = g_selfNextId++;
                    g_selfRecords.push_back(r);
                    return r.id;
                }

                // Matched by id, not by index. A ring that wraps while a
                // long action is open would otherwise close whichever
                // record had been recycled into that slot, silently
                // attributing one action's duration to another.
                inline void End(unsigned long long id) {
                    if (!id) return;
                    std::lock_guard<std::mutex> lock(g_selfMutex);
                    for (auto& r : g_selfRecords) {
                        if (r.id == id) { r.endUtc = NowUtc(); r.open = false; return; }
                    }
                    // Aged out of the ring during a very long action.
                    // Nothing to close, and inventing a record would be
                    // worse than losing one.
                }

                // RAII, so an early return or a throw cannot strand an
                // interval open forever - an open record overlaps every
                // later stall (Candidate::Overlaps treats it as unbounded),
                // so a leaked one would attribute the rest of the session
                // to OptiMax. Same shape as ScopedHandle and ScopedEvt.
                class Scope {
                public:
                    explicit Scope(const char* what) : m_id(Begin(what)) {}
                    ~Scope() { End(m_id); }
                    Scope(const Scope&) = delete;
                    Scope& operator=(const Scope&) = delete;
                private:
                    unsigned long long m_id;
                };

                inline std::vector<Record> Snapshot() {
                    std::lock_guard<std::mutex> lock(g_selfMutex);
                    return g_selfRecords;
                }

                // Test seam. The harness needs to drive this ledger without
                // a real restore point, and a session that has already run
                // must not leak records into the next test.
                inline void ResetForTest() {
                    std::lock_guard<std::mutex> lock(g_selfMutex);
                    g_selfRecords.clear();
                    g_selfNextId = 1;
                }
                inline void RecordForTest(const char* what, ULONGLONG startUtc, ULONGLONG endUtc) {
                    std::lock_guard<std::mutex> lock(g_selfMutex);
                    EvictIfFullLocked();   // the same path Begin takes - see above
                    Record r;
                    r.startUtc = startUtc;
                    r.endUtc = endUtc;
                    r.open = (endUtc == 0);
                    r.what = what;
                    r.id = g_selfNextId++;
                    g_selfRecords.push_back(r);
                }
            }

            // ==========================================================
            // CPU CONTENTION - the cause the event logs structurally
            // cannot see.
            //
            // 🎯 WHY THIS EXISTS. Windows writes no event when another
            // program eats your CPU, so every stall caused by ordinary
            // contention - a compile, a stream encode, a browser with
            // ninety tabs - came back "unattributed - no matching system
            // event was found". That answer was honest and useless, and
            // it is the most common real cause of stutter.
            //
            // 🔴 GROUPED BY EXECUTABLE NAME, NOT BY PID, and that is the
            // whole design. The obvious implementation reports the single
            // hungriest process. Measured against this project's own
            // stall inducer - 24 concurrent powershell.exe spinners - the
            // hungriest process held about 4% of the machine, so a
            // top-process report would have named something irrelevant
            // while 24 copies of one program saturated every core. Real
            // workloads look like this constantly (Chrome, MSBuild, any
            // game launcher). Aggregating by name turns that into
            // "powershell.exe - 24 processes, 52% of the CPU".
            //
            // ⚠️ SATURATION IS THE SIGNAL, NOT PER-PROCESS CPU. A game
            // legitimately pinning one core is not a stall cause, and
            // reporting it as one would be exactly the confident wrong
            // answer this file is written to avoid. A candidate is raised
            // only when the machine as a whole was oversubscribed; the
            // per-name breakdown then says who was responsible.
            // ==========================================================
            namespace ProcessLoad {

                struct NameLoad {
                    std::wstring name;
                    ULONGLONG cpu100ns = 0;
                    int processCount = 0;
                };

                struct Sample {
                    ULONGLONG takenUtc = 0;
                    std::vector<NameLoad> byName;   // sorted by name
                    bool valid = false;
                };

                // A stall means "more runnable threads than cores", so the
                // bar is machine-wide saturation. 85% rather than 100%
                // because GetProcessTimes rounds to the scheduler quantum
                // and a genuinely pegged machine measures in the low 90s;
                // requiring 100% would report nothing on the very case
                // this is for.
                inline constexpr double kSaturatedPercent = 85.0;

                inline int CoreCount() {
                    SYSTEM_INFO si;
                    GetSystemInfo(&si);
                    return si.dwNumberOfProcessors ? (int)si.dwNumberOfProcessors : 1;
                }

                // Local RAII rather than OptiMaxEngine.h's ScopedHandle:
                // Engine includes THIS header, so reaching the other way
                // would be a cycle.
                class AutoClose {
                public:
                    explicit AutoClose(HANDLE h) : m_h(h) {}
                    ~AutoClose() { if (m_h && m_h != INVALID_HANDLE_VALUE) CloseHandle(m_h); }
                    AutoClose(const AutoClose&) = delete;
                    AutoClose& operator=(const AutoClose&) = delete;
                    operator HANDLE() const { return m_h; }
                private:
                    HANDLE m_h;
                };

                inline std::wstring BaseName(const std::wstring& path) {
                    const size_t slash = path.find_last_of(L"\\/");
                    std::wstring n = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
                    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
                    return n;
                }

                // Cumulative kernel+user CPU per executable name.
                //
                // ⚠️ An invalid Sample is NOT an empty one. If the
                // enumeration fails we must be able to say we could not
                // look, for the same reason ProbeStatus has three values
                // and not two - an empty result would read as "nothing was
                // using the CPU", which is a finding we did not make.
                inline Sample Take() {
                    Sample s;
                    s.takenUtc = NowUtc();

                    std::vector<DWORD> pids(1024);
                    DWORD needed = 0;
                    while (true) {
                        if (!EnumProcesses(pids.data(),
                            (DWORD)(pids.size() * sizeof(DWORD)), &needed)) {
                            return s;            // stays invalid
                        }
                        if (needed < pids.size() * sizeof(DWORD)) break;
                        if (pids.size() >= 65536) break;
                        pids.resize(pids.size() * 2);
                    }

                    const size_t count = needed / sizeof(DWORD);
                    std::vector<NameLoad> flat;
                    flat.reserve(count);

                    for (size_t i = 0; i < count; ++i) {
                        if (!pids[i]) continue;
                        AutoClose h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]));
                        if (!(HANDLE)h) continue;   // protected or exited - normal, not an error

                        FILETIME ftCreate{}, ftExit{}, ftKernel{}, ftUser{};
                        if (!GetProcessTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser)) continue;

                        wchar_t path[MAX_PATH];
                        DWORD sz = MAX_PATH;
                        if (!QueryFullProcessImageNameW(h, 0, path, &sz)) continue;

                        NameLoad nl;
                        nl.name = BaseName(std::wstring(path, sz));
                        nl.cpu100ns = FileTimeToU64(ftKernel) + FileTimeToU64(ftUser);
                        nl.processCount = 1;
                        flat.push_back(std::move(nl));
                    }

                    std::sort(flat.begin(), flat.end(),
                        [](const NameLoad& a, const NameLoad& b) { return a.name < b.name; });

                    for (auto& nl : flat) {
                        if (!s.byName.empty() && s.byName.back().name == nl.name) {
                            s.byName.back().cpu100ns += nl.cpu100ns;
                            s.byName.back().processCount += nl.processCount;
                        }
                        else {
                            s.byName.push_back(std::move(nl));
                        }
                    }

                    s.valid = true;
                    return s;
                }

                struct Contributor {
                    std::wstring name;
                    double percentOfMachine = 0.0;
                    int processCount = 0;
                };

                struct LoadReport {
                    bool valid = false;          // false == we could not look
                    double busyPercent = 0.0;    // across all cores
                    ULONGLONG spanMs = 0;
                    std::vector<Contributor> top;
                    bool Saturated() const { return valid && busyPercent >= kSaturatedPercent; }
                };

                // Difference two samples into "who used the machine".
                inline LoadReport Between(const Sample& a, const Sample& b, size_t maxN = 3) {
                    LoadReport r;
                    if (!a.valid || !b.valid || b.takenUtc <= a.takenUtc) return r;

                    const ULONGLONG span100ns = b.takenUtc - a.takenUtc;
                    r.spanMs = span100ns / 10000ULL;
                    if (r.spanMs == 0) return r;

                    const double capacity = (double)span100ns * (double)CoreCount();
                    if (capacity <= 0.0) return r;

                    size_t ia = 0;
                    ULONGLONG totalDelta = 0;
                    std::vector<Contributor> all;

                    for (const auto& later : b.byName) {
                        while (ia < a.byName.size() && a.byName[ia].name < later.name) ++ia;

                        ULONGLONG before = 0;
                        if (ia < a.byName.size() && a.byName[ia].name == later.name) {
                            before = a.byName[ia].cpu100ns;
                        }

                        // ⚠️ Clamped, never allowed to go negative. Processes
                        // come and go between samples, so a name's total can
                        // legitimately FALL when one of several copies exits.
                        // Unclamped, that underflows ULONGLONG into an
                        // astronomical delta and the exited program becomes
                        // the reported culprit.
                        const ULONGLONG delta = (later.cpu100ns > before)
                            ? later.cpu100ns - before : 0;
                        if (delta == 0) continue;

                        totalDelta += delta;
                        Contributor c;
                        c.name = later.name;
                        c.percentOfMachine = (double)delta / capacity * 100.0;
                        c.processCount = later.processCount;
                        all.push_back(std::move(c));
                    }

                    r.busyPercent = (double)totalDelta / capacity * 100.0;
                    // Sampling granularity can push this a hair over 100.
                    if (r.busyPercent > 100.0) r.busyPercent = 100.0;

                    std::sort(all.begin(), all.end(),
                        [](const Contributor& x, const Contributor& y) {
                            return x.percentOfMachine > y.percentOfMachine;
                        });
                    if (all.size() > maxN) all.resize(maxN);
                    r.top = std::move(all);
                    r.valid = true;
                    return r;
                }

                // The rolling pair. Sampled from the telemetry tick, which
                // is also the thread that goes quiet during a stall - so
                // the two most recent samples naturally BRACKET the stall
                // rather than sitting inside it. That is the behaviour we
                // want and it costs nothing to get: the tick that came back
                // 11 seconds late is exactly the one whose span covers the
                // freeze.
                inline std::mutex g_loadMutex;
                inline Sample g_prevSample;
                inline Sample g_currSample;

                inline void PushSample() {
                    Sample s = Take();            // taken OUTSIDE the lock
                    std::lock_guard<std::mutex> lock(g_loadMutex);
                    g_prevSample = std::move(g_currSample);
                    g_currSample = std::move(s);
                }

                inline LoadReport Latest(size_t maxN = 3) {
                    Sample a, b;
                    {
                        std::lock_guard<std::mutex> lock(g_loadMutex);
                        a = g_prevSample;
                        b = g_currSample;
                    }
                    return Between(a, b, maxN);
                }

                // 🔴 THE SEVERE-STALL CASE, and it was found by running the
                // real app rather than by reading the code.
                //
                // PushSample() lives on the telemetry tick, and during a hard
                // stall that tick does not run at all - one measured 34
                // SECONDS in a single iteration. So the stored pair can be
                // entirely older than the stall, and differencing it describes
                // the quiet minute BEFORE the freeze instead of the freeze.
                // Live proof: two identical bursts, one reporting
                // "powershell.exe (25 processes, 97%)" because the tick still
                // managed to cycle, the other reporting "unattributed" because
                // it did not. The worse the stall, the less likely the feature
                // was to work - exactly backwards.
                //
                // The watchdog thread, which is what calls this, keeps running
                // throughout. So when the newest stored sample predates the
                // stall, take a fresh one HERE and pair it with the last
                // pre-stall sample. That brackets the stall by construction.
                //
                // ⚠️ Costs one process enumeration (6-8ms idle, more under the
                // load that caused the stall). Accepted because it sits inside
                // the attribution block, which is already rate-limited to once
                // per 5s and already pays far more than this for the event-log
                // query - measured at ~8s under 20 spinners. Never call it
                // outside that block.
                inline LoadReport LatestBracketing(ULONGLONG stallStartUtc, size_t maxN = 3) {
                    Sample a, b;
                    {
                        std::lock_guard<std::mutex> lock(g_loadMutex);
                        a = g_prevSample;
                        b = g_currSample;
                    }

                    if (b.valid && b.takenUtc <= stallStartUtc) {
                        Sample fresh = Take();
                        // Deliberately NOT stored back into g_currSample. The
                        // rolling pair belongs to the telemetry tick's cadence;
                        // injecting an off-schedule sample would shorten the
                        // next tick's window and understate whatever it covers.
                        if (fresh.valid) return Between(b, fresh, maxN);
                    }
                    return Between(a, b, maxN);
                }

                inline void ResetForTest() {
                    std::lock_guard<std::mutex> lock(g_loadMutex);
                    g_prevSample = Sample{};
                    g_currSample = Sample{};
                }

                // Injects a bracketing pair so the harness can drive real
                // saturation arithmetic without having to actually peg the
                // machine. The production path (PushSample) and this one
                // both end at the same two variables Latest() reads, so
                // there is no second code path to drift.
                inline void SetSamplesForTest(const Sample& prev, const Sample& curr) {
                    std::lock_guard<std::mutex> lock(g_loadMutex);
                    g_prevSample = prev;
                    g_currSample = curr;
                }

                // "powershell.exe (24 processes, 52%)" - built here so the
                // log line and the on-screen report cannot drift apart.
                inline std::string DescribeContributor(const Contributor& c) {
                    // A real conversion, not the iterator-range narrowing this
                    // project used to use everywhere - an executable name is a
                    // path fragment and is under no obligation to be ASCII.
                    // See OptiMaxText.h.
                    std::string s = Text::ToUtf8(c.name);
                    if (c.processCount > 1) {
                        s += " (" + std::to_string(c.processCount) + " processes, ";
                    }
                    else {
                        s += " (";
                    }
                    s += std::to_string((int)(c.percentOfMachine + 0.5)) + "%)";
                    return s;
                }
            }

            inline int ExplanatoryWeight(CauseKind k) {
                switch (k) {
                // Above every Windows cause on purpose. If OptiMax was
                // making a restore point while the machine stalled, that
                // is the answer, and naming a Defender scan that merely
                // overlapped would be blaming Windows for our own work.
                case CauseKind::OptiMaxItself:   return 200;
                case CauseKind::DisplayReset:    return 100;
                // Below a named Windows event on purpose. A TDR or a scan is
                // a specific finding somebody else recorded; saturation is
                // our own measurement and a saturated CPU is sometimes just
                // a busy machine. It still outranks the disk signals, since
                // contention explains a stall far more directly than a
                // single retried I/O does.
                case CauseKind::ProcessContention: return 70;
                case CauseKind::DefenderScan:    return 80;
                case CauseKind::DiskReset:       return 60;
                case CauseKind::DiskRetry:       return 40;
                case CauseKind::PowerTransition: return 20;
                case CauseKind::RawScanEvent:    return 0;   // never ranked
                }
                return 0;
            }

            struct Report {
                std::vector<Candidate> candidates;   // ranked, strongest first
                std::vector<ProbeOutcome> probes;

                // True only when every probe genuinely ran. If any failed, the
                // absence of candidates says nothing and must not be phrased as
                // though it did.
                bool AllProbesRan() const {
                    for (const auto& p : probes) {
                        if (p.status != ProbeStatus::ProbedOk) return false;
                    }
                    return true;
                }
                bool Attributed() const { return !candidates.empty(); }
            };

            // Correlates one stall window against every source.
            //
            // Two different windows are used on purpose. Defender is queried
            // over a long lookback, because a scan that started hours ago can
            // still be running now. The System point events are queried over
            // the narrow stall window only - a disk retry four hours ago has
            // nothing to do with this stall, and including it would produce
            // confident nonsense.
            //
            // slackMs widens the narrow window on both sides: a cause that
            // fires 300ms before the monitor notices is still the cause, and
            // event timestamps are only as precise as the writing provider.
            inline Report AttributeWindow(ULONGLONG stallStartUtc,
                ULONGLONG stallEndUtc,
                ULONGLONG slackMs = 2000,
                ULONGLONG lookbackMs = kScanLookbackMs) {
                Report r;
                const ULONGLONG slack = slackMs * 10000ULL;
                const ULONGLONG lookback = lookbackMs * 10000ULL;

                const ULONGLONG nearFrom = (stallStartUtc > slack) ? stallStartUtc - slack : 0;
                const ULONGLONG nearTo = stallEndUtc + slack;
                const ULONGLONG farFrom = (stallStartUtc > lookback) ? stallStartUtc - lookback : 0;

                // OUR OWN WORK FIRST - free, already recorded, and it needs
                // no probe. Deliberately outside the ProbeOutcome machinery:
                // this ledger cannot be "unavailable", so folding it in
                // there would invent a failure mode that does not exist and
                // would let an unreadable Defender channel suppress an
                // answer we already hold.
                //
                // ⚠️ COLLAPSED TO AT MOST ONE CANDIDATE, deliberately. These
                // scopes NEST - applying core tweaks takes a restore point
                // inside itself - so a single stall routinely overlaps two or
                // three records. Pushing them all would rank them equally
                // (same weight) and render as "OptiMax itself - taking a
                // system restore point, plus 2 weaker signals in the same
                // window", which invites the reader to count three separate
                // problems where there was one. "OptiMax was busy" is one
                // fact. The longest-running overlapping action is the one
                // named, because it dominated the window; an action still
                // open has no known end and so always wins.
                {
                    // ⚠️ Held in a NAMED local. Iterating Snapshot() directly
                    // would extend the returned temporary only to the end of
                    // the range-for, so the pointer picked out of it would
                    // dangle by the time it is read below.
                    const std::vector<SelfActivity::Record> selfRecs = SelfActivity::Snapshot();
                    const SelfActivity::Record* best = nullptr;
                    ULONGLONG bestSpan = 0;
                    for (const auto& rec : selfRecs) {
                        Candidate probe;
                        probe.startUtc = rec.startUtc;
                        probe.endUtc = rec.endUtc;
                        probe.endOpen = rec.open;
                        if (!probe.Overlaps(nearFrom, nearTo)) continue;

                        const ULONGLONG span = rec.open ? ~0ULL
                            : (rec.endUtc > rec.startUtc ? rec.endUtc - rec.startUtc : 0);
                        if (!best || span > bestSpan) { best = &rec; bestSpan = span; }
                    }
                    if (best) {
                        Candidate c;
                        c.kind = CauseKind::OptiMaxItself;
                        c.startUtc = best->startUtc;
                        c.endUtc = best->endUtc;
                        c.endOpen = best->open;
                        c.selfWhat = best->what;
                        r.candidates.push_back(c);
                    }
                }

                // CPU CONTENTION - our own measurement, so like the self
                // ledger it needs no probe and cannot be "unavailable".
                // Raised ONLY when the machine as a whole was oversubscribed;
                // see the note on kSaturatedPercent for why per-process CPU
                // on its own is not evidence of anything.
                {
                    const ProcessLoad::LoadReport load = ProcessLoad::LatestBracketing(stallStartUtc);
                    if (load.Saturated() && !load.top.empty()) {
                        Candidate c;
                        c.kind = CauseKind::ProcessContention;
                        // The samples bracket the stall, so the window they
                        // cover is the honest extent of this finding.
                        c.startUtc = stallStartUtc;
                        c.endUtc = stallEndUtc;
                        c.detail = std::to_string((int)(load.busyPercent + 0.5))
                            + "% of the CPU was in use - ";
                        for (size_t i = 0; i < load.top.size(); ++i) {
                            if (i) c.detail += ", ";
                            c.detail += ProcessLoad::DescribeContributor(load.top[i]);
                        }
                        r.candidates.push_back(std::move(c));
                    }
                }

                std::vector<Candidate> rawScans;
                r.probes.push_back(QueryChannel(kDefenderChannel,
                    { kDefenderScanStarted, kDefenderScanCompleted, kDefenderScanStopped },
                    farFrom, nearTo, rawScans));

                for (auto& iv : BuildScanIntervals(rawScans, farFrom)) {
                    if (iv.Overlaps(nearFrom, nearTo)) r.candidates.push_back(iv);
                }

                r.probes.push_back(QueryChannel(kSystemChannel, { 4101, 129, 153 },
                    nearFrom, nearTo, r.candidates));

                // Strongest explanation first; among equals, the one closest to
                // the stall. An overlapping interval has distance zero, so a
                // running scan always beats one that merely ended nearby.
                std::sort(r.candidates.begin(), r.candidates.end(),
                    [stallStartUtc](const Candidate& a, const Candidate& b) {
                        const int wa = ExplanatoryWeight(a.kind), wb = ExplanatoryWeight(b.kind);
                        if (wa != wb) return wa > wb;
                        auto dist = [stallStartUtc](const Candidate& c) -> ULONGLONG {
                            if (c.Overlaps(stallStartUtc, stallStartUtc)) return 0;
                            return (c.startUtc > stallStartUtc)
                                ? c.startUtc - stallStartUtc : stallStartUtc - c.startUtc;
                        };
                        return dist(a) < dist(b);
                    });
                return r;
            }

            inline const char* KindPhrase(CauseKind k) {
                switch (k) {
                case CauseKind::OptiMaxItself:   return "OptiMax's own background work";
                case CauseKind::ProcessContention: return "another program was using the CPU";
                case CauseKind::DisplayReset:    return "the display driver reset (TDR)";
                case CauseKind::DefenderScan:    return "a Windows Defender scan was running";
                case CauseKind::DiskReset:       return "the disk was reset after a timeout";
                case CauseKind::DiskRetry:       return "the disk retried an I/O operation";
                case CauseKind::PowerTransition: return "a power state transition";
                case CauseKind::RawScanEvent:    return "a Windows Defender scan event";
                }
                return "an unrecognised system event";
            }

            // The sentence the user actually reads. Deliberately built here
            // rather than in the UI so that the log line and the on-screen
            // report cannot drift apart, which is how the version string
            // ended up in ten places.
            inline std::string Describe(const Report& r) {
                if (r.Attributed()) {
                    const Candidate& top = r.candidates.front();
                    std::string s = KindPhrase(top.kind);

                    // 🔴 SAID PLAINLY AND FIRST, and phrased as OptiMax's
                    // fault rather than as a neutral observation. The whole
                    // value of this branch is that the user is told the tool
                    // they installed to stop stutter just caused some; a
                    // hedged wording here would defeat the point of
                    // recording it at all.
                    if (top.kind == CauseKind::OptiMaxItself) {
                        s = "OptiMax itself";
                        if (top.selfWhat) { s += " - "; s += top.selfWhat; }
                        s += top.endOpen ? ", which was still running when the stall was measured"
                                         : "";
                        if (const ULONGLONG ms = top.DurationMs()) {
                            s += " (took " + std::to_string(ms) + "ms)";
                        }
                        // We know it was us regardless of whether the event
                        // logs could be read - this candidate needed no
                        // probe. But a failed probe still means something
                        // ELSE may also have been happening, and silently
                        // dropping that would overstate a complete answer.
                        if (!r.AllProbesRan()) {
                            s += "; one of the system logs could not be read, "
                                 "so anything else in that window is unknown";
                        }
                        return s;
                    }

                    if (top.kind == CauseKind::DefenderScan) {
                        if (top.endOpen) {
                            // NOT "had not finished" - that is a claim about the
                            // scan, and all we observed is the absence of an end
                            // record inside the window we queried. Live, those
                            // coincide (the scan really has not ended yet); on a
                            // historical window they do not, and the stronger
                            // wording would have stated as fact something we
                            // simply did not look far enough forward to see.
                            s += " and was still running when the stall was measured";
                        }
                        else if (const ULONGLONG ms = top.DurationMs()) {
                            s += " (" + std::to_string(ms / 1000) + "s";
                            if (top.startedBeforeLookback) s += " visible, it began earlier";
                            s += ")";
                        }
                    }
                    if (top.kind == CauseKind::ProcessContention) {
                        s += " (" + top.detail + ")";
                    }

                    // ⚠️ CONTENTION IS NAMED EVEN WHEN IT IS NOT THE TOP
                    // CANDIDATE, instead of being swallowed by the "plus N
                    // weaker signals" count below. A Defender scan outranks
                    // it, but "a scan was running AND the CPU was at 98%,
                    // mostly MsMpEng.exe" is a materially better answer than
                    // the scan alone - the two facts corroborate each other,
                    // and the breakdown is the part the user can act on.
                    // Anonymous signal counting is fine for a disk retry; it
                    // is a waste of the only measurement here that names
                    // names.
                    size_t anonymous = 0;
                    for (size_t i = 1; i < r.candidates.size(); ++i) {
                        const Candidate& c = r.candidates[i];
                        if (c.kind == CauseKind::ProcessContention && !c.detail.empty()) {
                            s += "; the CPU was also busy (" + c.detail + ")";
                        }
                        else {
                            ++anonymous;
                        }
                    }
                    if (anonymous > 0) {
                        s += ", plus " + std::to_string(anonymous)
                            + " weaker signal" + (anonymous == 1 ? "" : "s")
                            + " in the same window";
                    }
                    return s;
                }
                if (!r.AllProbesRan()) {
                    // NOT "unattributed". We did not look, and saying we looked
                    // would be the same lie as a toggle reporting success for a
                    // write it never made.
                    return "not determined - one of the system logs could not be read";
                }
                return "unattributed - no matching system event was found";
            }

            // Used when the watchdog DECLINED to run a fresh query - see the
            // cooldown in OptiMaxDiagnostics.h.
            //
            // 🔴 IT DELIBERATELY DOES NOT RESTATE THE PREVIOUS CAUSE AS THIS
            // STALL'S CAUSE, which is the obvious implementation and is wrong.
            // A Defender scan that explained a stall five seconds ago may have
            // finished since; a disk retry certainly does not explain a later
            // stall - it is an instant, not an interval. Carrying the cause
            // forward would manufacture confident attributions for stalls
            // nobody looked at, which is the exact failure this whole component
            // was written to avoid.
            //
            // What IS honest: say we did not look, say why, and report what the
            // last real query found as context the reader can weigh themselves.
            inline std::string DescribeRateLimited(const Report& prev, bool havePrev,
                ULONGLONG msSincePrev) {
                std::string s = "not checked - attribution is rate-limited so the watchdog "
                    "keeps measuring";
                if (havePrev) {
                    // Sub-second gaps rendered as "0s earlier", which is truthful
                    // integer division and reads like a bug. Stalls arriving
                    // within the same second are exactly what the cooldown is for,
                    // so this is the common case, not an edge one.
                    const ULONGLONG secs = msSincePrev / 1000ULL;
                    s += (secs == 0)
                        ? " (the stall moments earlier was "
                        : (" (the stall " + std::to_string(secs) + "s earlier was ");
                    s += prev.Attributed()
                        ? ("attributed to: " + Describe(prev) + ")")
                        : ("unattributed)");
                }
                return s;
            }

        }
    }
}
