#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <mutex>

namespace worldgen {

/**
 * Simple profiling utility for measuring execution time of code sections.
 * Thread-safe for concurrent access.
 *
 * Usage:
 *   PROFILE_SCOPE("MyFunction");           // Times until scope exit
 *   PROFILE_FUNCTION();                    // Times entire function
 *
 *   Profiler::instance().start("section"); // Manual start
 *   Profiler::instance().stop("section");  // Manual stop
 *
 *   Profiler::instance().report();         // Print results
 */
class Profiler {
public:
    static Profiler& instance() {
        static Profiler p;
        return p;
    }

    void start(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_starts[name] = std::chrono::high_resolution_clock::now();
    }

    void stop(const std::string& name) {
        auto end = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_starts.find(name);
        if (it != m_starts.end()) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end - it->second).count();
            m_totals[name] += duration;
            m_counts[name]++;

            // Track min/max
            if (m_mins.find(name) == m_mins.end() || duration < m_mins[name]) {
                m_mins[name] = duration;
            }
            if (duration > m_maxs[name]) {
                m_maxs[name] = duration;
            }
        }
    }

    void report() const {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_totals.empty()) {
            std::cout << "\n[Profiler] No data collected.\n";
            return;
        }

        // Sort by total time descending
        std::vector<std::pair<std::string, long long>> sorted(m_totals.begin(), m_totals.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        long long grandTotal = 0;
        for (const auto& [name, total] : sorted) {
            grandTotal += total;
        }

        std::cout << "\n";
        std::cout << "╔══════════════════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                            PROFILING REPORT                                  ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ " << std::left << std::setw(28) << "Section"
                  << std::right << std::setw(10) << "Total(ms)"
                  << std::setw(8) << "Calls"
                  << std::setw(10) << "Avg(us)"
                  << std::setw(10) << "Min(us)"
                  << std::setw(10) << "Max(us)"
                  << std::setw(6) << "%" << " ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣\n";

        for (const auto& [name, total] : sorted) {
            auto count = m_counts.at(name);
            auto minTime = m_mins.count(name) ? m_mins.at(name) : 0;
            auto maxTime = m_maxs.count(name) ? m_maxs.at(name) : 0;
            double pct = grandTotal > 0 ? (100.0 * total / grandTotal) : 0.0;

            // Truncate long names
            std::string displayName = name;
            if (displayName.length() > 27) {
                displayName = displayName.substr(0, 24) + "...";
            }

            std::cout << "║ " << std::left << std::setw(28) << displayName
                      << std::right << std::setw(10) << std::fixed << std::setprecision(1)
                      << (total / 1000.0)
                      << std::setw(8) << count
                      << std::setw(10) << (count > 0 ? total / count : 0)
                      << std::setw(10) << minTime
                      << std::setw(10) << maxTime
                      << std::setw(5) << std::setprecision(1) << pct << "%" << " ║\n";
        }

        std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ " << std::left << std::setw(28) << "TOTAL"
                  << std::right << std::setw(10) << std::fixed << std::setprecision(1)
                  << (grandTotal / 1000.0)
                  << std::setw(44) << " " << " ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n";
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_starts.clear();
        m_totals.clear();
        m_counts.clear();
        m_mins.clear();
        m_maxs.clear();
    }

    // Get specific metrics for programmatic access
    long long getTotalMicroseconds(const std::string& name) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_totals.find(name);
        return it != m_totals.end() ? it->second : 0;
    }

    int getCallCount(const std::string& name) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_counts.find(name);
        return it != m_counts.end() ? it->second : 0;
    }

private:
    Profiler() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> m_starts;
    std::unordered_map<std::string, long long> m_totals;  // microseconds
    std::unordered_map<std::string, int> m_counts;
    std::unordered_map<std::string, long long> m_mins;
    std::unordered_map<std::string, long long> m_maxs;
};

/**
 * RAII timer that automatically measures scope duration.
 */
class ScopedTimer {
public:
    explicit ScopedTimer(const std::string& name) : m_name(name) {
        Profiler::instance().start(m_name);
    }

    ~ScopedTimer() {
        Profiler::instance().stop(m_name);
    }

    // Non-copyable
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::string m_name;
};

// Concatenation helper for unique variable names
#define PROFILER_CONCAT_IMPL(a, b) a##b
#define PROFILER_CONCAT(a, b) PROFILER_CONCAT_IMPL(a, b)

/**
 * Profile a named scope. Use at the start of a block to measure its duration.
 * Example: PROFILE_SCOPE("RenderLoop");
 */
#define PROFILE_SCOPE(name) \
    worldgen::ScopedTimer PROFILER_CONCAT(_profiler_timer_, __LINE__)(name)

/**
 * Profile the current function. Use at the start of a function.
 * Example: PROFILE_FUNCTION();
 */
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)

/**
 * Conditionally enable/disable profiling at compile time.
 * Define WORLDGEN_DISABLE_PROFILING to remove all profiling overhead.
 */
#ifdef WORLDGEN_DISABLE_PROFILING
    #undef PROFILE_SCOPE
    #undef PROFILE_FUNCTION
    #define PROFILE_SCOPE(name) ((void)0)
    #define PROFILE_FUNCTION() ((void)0)
#endif

} // namespace worldgen
