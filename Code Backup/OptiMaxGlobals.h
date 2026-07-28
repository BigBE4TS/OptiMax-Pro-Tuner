#pragma once
#include <string>
#include <vector>
#include <mutex>

// Global Hardware States
extern bool g_IsAmdGpu;
extern bool g_IsLowSpec;
extern bool g_IsHeterogeneousCpu;

// Global Logging
extern void SafeAddLog(const std::string& msg);