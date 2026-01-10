#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <iostream>
#include <iomanip>

namespace worldgen {

class Profiler {
public:
    static Profiler& instance() {
        static Profiler p;
        return p;
    }

    void start(const std::string& name) {
        m_starts[name] = std::chrono::high_resolution_clock::now();
    }

    void stop(const std::string& name) {
        auto end = std::chrono::high_resolution_clock::now();
        auto it = m_starts.find(name);
        if (it != m_starts.end()) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end - it->second).count();
            m_totals[name] += duration;
            m_counts[name]++;
        }
    }

    void report() const {
        std::cout << "\n=== PROFILING REPORT ===\n";
        std::cout << std::left << std::setw(35) << "Section"
                  << std::right << std::setw(12) << "Total (ms)"
                  << std::setw(10) << "Calls"
                  << std::setw(12) << "Avg (us)\n";
        std::cout << std::string(69, '-') << "\n";

        for (const auto& [name, total] : m_totals) {
            auto count = m_counts.at(name);
            std::cout << std::left << std::setw(35) << name
                      << std::right << std::setw(12) << std::fixed
                      << std::setprecision(2) << (total / 1000.0)
                      << std::setw(10) << count
                      << std::setw(12) << (count > 0 ? total / count : 0)
                      << "\n";
        }
        std::cout << "========================\n";
    }

    void reset() {
        m_starts.clear();
        m_totals.clear();
        m_counts.clear();
    }

private:
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> m_starts;
    std::unordered_map<std::string, long long> m_totals;
    std::unordered_map<std::string, int> m_counts;
};

// RAII scope timer
class ScopedTimer {
public:
    ScopedTimer(const std::string& name) : m_name(name) {
        Profiler::instance().start(m_name);
    }
    ~ScopedTimer() {
        Profiler::instance().stop(m_name);
    }
private:
    std::string m_name;
};

#define PROFILE_SCOPE(name) worldgen::ScopedTimer _timer##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)

} // namespace worldgen
