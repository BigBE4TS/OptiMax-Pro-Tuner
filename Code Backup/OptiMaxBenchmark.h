#pragma once
#include <windows.h>
#include <vector>
#include <numeric>
#include <algorithm>

namespace OptiMax {
    namespace Benchmark {
        struct AuditResult {
            double averageDPC;
            double maxDPC;
            double onePercentHighDPC;
        };

        inline AuditResult CalculateAudit(std::vector<double>& samples) {
            AuditResult result = { 0.0, 0.0, 0.0 };
            if (samples.empty()) return result;

            std::sort(samples.begin(), samples.end());

            double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
            result.averageDPC = sum / samples.size();
            result.maxDPC = samples.back();

            size_t onePercentIndex = (size_t)(samples.size() * 0.99);
            if (onePercentIndex >= samples.size()) onePercentIndex = samples.size() - 1;

            result.onePercentHighDPC = samples[onePercentIndex];
            return result;
        }
    }
}