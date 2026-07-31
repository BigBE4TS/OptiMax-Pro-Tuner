#pragma once
#include <string>

// Global Hardware States
extern bool g_IsAmdGpu;
extern bool g_IsLowSpec;

// g_IsHeterogeneousCpu lived here and was removed. It was never read and never
// written - a placeholder for P-core/E-core awareness that was never wired up, and
// the reason it was left alone earlier was that deleting it would erase the only
// trace of that intent.
//
// That intent is now actually implemented, so the placeholder no longer stands in
// for anything: V3::BuildPerformanceCoreMask() (OptiMaxExperimental.h) reads real
// EfficiencyClass topology, LockToPCores refuses to run when a CPU has no split,
// and OptiMaxApp.h's isHeterogeneousCPU is derived from the same call instead of
// from the CPU's marketing name. A cached bool would only be a third place for that
// answer to drift out of step with the other two.

// Global Logging
extern void SafeAddLog(const std::string& msg);