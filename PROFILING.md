# Profiling Workflow for World Generator

This document outlines how to profile the terrain generator, identify bottlenecks, and plan performance improvements.

## Quick Start

```bash
# 1. Build with debug symbols (release + symbols for profiling)
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake --build . -j$(sysctl -n hw.ncpu)

# 2. Run with Instruments (macOS)
instruments -t "Time Profiler" ./worldgen

# 3. Or add timing instrumentation (see below)
```

---

## Part 1: Profiling Tools

### macOS Instruments (Recommended)

**Time Profiler** - CPU hotspot analysis:
```bash
# GUI method
open -a Instruments
# Select "Time Profiler" template, then choose ./build/worldgen

# CLI method
instruments -t "Time Profiler" -D profile_output.trace ./build/worldgen
```

**Allocations** - Memory profiling:
```bash
instruments -t "Allocations" ./build/worldgen
```

**System Trace** - Thread activity and scheduling:
```bash
instruments -t "System Trace" ./build/worldgen
```

### Sample-based Profiling (Quick CLI)

```bash
# Sample running process every 1ms for 30 seconds
sample worldgen 30 -file profile.txt

# Or attach to PID
sample <PID> 10 -file profile.txt
```

### Perf-like Analysis with dtrace

```bash
# CPU flame graph data collection
sudo dtrace -x ustackframes=100 -n 'profile-997 /execname == "worldgen"/ { @[ustack()] = count(); }' -o out.stacks
```

---

## Part 2: Built-in Timing Instrumentation

Add this utility header for manual profiling:

### `src/utils/Profiler.hpp`

```cpp
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
        std::cout << std::left << std::setw(30) << "Section"
                  << std::right << std::setw(12) << "Total (ms)"
                  << std::setw(10) << "Calls"
                  << std::setw(12) << "Avg (us)\n";
        std::cout << std::string(64, '-') << "\n";

        for (const auto& [name, total] : m_totals) {
            auto count = m_counts.at(name);
            std::cout << std::left << std::setw(30) << name
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
```

### Usage in Code

```cpp
// In HydraulicErosion.cpp
void HydraulicErosion::step() {
    PROFILE_SCOPE("HydraulicErosion::step");

    for (int i = 0; i < m_dropletsPerStep; ++i) {
        PROFILE_SCOPE("simulateDroplet");
        simulateDroplet();
    }
}

// In Application.cpp (end of session)
Profiler::instance().report();
```

---

## Part 3: Profiling Workflow

### Step 1: Establish Baseline

```bash
# Build release with debug symbols
./build.sh -c
cd build && cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .. && cmake --build . -j8

# Run and time full generation
time ./worldgen
```

Record baseline metrics:
- Total generation time
- Time per simulation step
- Memory usage (Activity Monitor or `top -pid <PID>`)

### Step 2: Identify CPU Hotspots

1. **Run Time Profiler:**
   ```bash
   instruments -t "Time Profiler" ./build/worldgen
   ```

2. **Look for these functions:**
   | Function | Expected % | Concern if > |
   |----------|-----------|--------------|
   | `HydraulicErosion::simulateDroplet` | 40-60% | 70% |
   | `ThermalErosion::step` | 10-20% | 30% |
   | `NoiseGenerator::generate` | 10-15% | 25% |
   | `HeightmapView::update` | 5-10% | 20% |
   | `ColorMapper::getColor` | <5% | 10% |

3. **Drill down into hotspots:**
   - Check call counts
   - Look for unexpected functions taking time
   - Identify lock contention (atomic operations)

### Step 3: Memory Analysis

```bash
# Check for memory leaks and allocation hotspots
instruments -t "Allocations" ./build/worldgen
```

**Key allocations to watch:**
- Erosion brush precomputation (4M entries × variable size)
- Heightmap allocations (3 × 2048×2048 × 4 bytes = 50MB)
- SDL texture memory

### Step 4: Thread Analysis

```bash
instruments -t "System Trace" ./build/worldgen
```

**Check for:**
- Thread utilization during OpenMP sections
- Lock contention in ThermalErosion atomics
- Main thread blocked on simulation

---

## Part 4: Known Bottlenecks & Optimization Plan

### Bottleneck 1: Hydraulic Erosion (Highest Impact)

**Current state:**
- 50,000 droplets per step, each ~30 iterations
- Sequential execution (cannot parallelize due to height writes)
- Random memory access pattern (poor cache locality)

**Optimizations:**

| Priority | Optimization | Expected Gain | Effort |
|----------|-------------|---------------|--------|
| P0 | Cache height calculation between iterations | 10-15% | Low |
| P0 | Reduce droplets per step during preview | 50%+ | Low |
| P1 | Spatial partitioning for parallel droplets | 30-50% | Medium |
| P1 | SIMD gradient calculation | 15-20% | Medium |
| P2 | GPU compute shader implementation | 5-10x | High |

**Quick win - reduce redundant calculations:**
```cpp
// Before: calculates height twice per iteration
auto [h1, g1] = calculateHeightAndGradient(posX, posY);
// ... move droplet ...
auto [h2, g2] = calculateHeightAndGradient(newPosX, newPosY);

// After: reuse previous calculation
HeightAndGradient current = calculateHeightAndGradient(posX, posY);
// ... move droplet ...
HeightAndGradient next = calculateHeightAndGradient(newPosX, newPosY);
float deltaHeight = next.height - current.height;  // Already have current.height
current = next;  // Reuse for next iteration
```

### Bottleneck 2: Thermal Erosion Atomic Contention

**Current state:**
- OpenMP parallel with atomic delta updates
- 8-neighbor writes cause contention

**Optimizations:**

| Priority | Optimization | Expected Gain | Effort |
|----------|-------------|---------------|--------|
| P0 | Thread-local delta buffers + reduction | 20-30% | Low |
| P1 | Red-black or checkerboard iteration | 15-25% | Medium |

**Thread-local buffer approach:**
```cpp
void ThermalErosion::step() {
    // Each thread accumulates to local buffer
    std::vector<std::vector<float>> localDeltas(omp_get_max_threads());
    for (auto& ld : localDeltas) ld.resize(width * height, 0.0f);

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        auto& delta = localDeltas[tid];

        #pragma omp for
        for (size_t y = 0; y < height; ++y) {
            // Accumulate to delta[tid] without atomics
        }
    }

    // Reduce all local buffers
    #pragma omp parallel for
    for (size_t i = 0; i < width * height; ++i) {
        float sum = 0;
        for (const auto& ld : localDeltas) sum += ld[i];
        heightData[i] += sum;
    }
}
```

### Bottleneck 3: Rendering During Simulation

**Current state:**
- Full heightmap re-render every frame
- Water overlay adds branching

**Optimizations:**

| Priority | Optimization | Expected Gain | Effort |
|----------|-------------|---------------|--------|
| P0 | Dirty rectangle tracking | 30-50% | Low |
| P1 | Double buffering for async updates | 20-30% | Medium |
| P1 | Separate water layer texture | 15-20% | Low |

### Bottleneck 4: Noise Generation

**Current state:**
- Already parallelized with OpenMP
- FastNoiseLite is optimized

**Optimizations:**

| Priority | Optimization | Expected Gain | Effort |
|----------|-------------|---------------|--------|
| P1 | SIMD batch noise (4-8 pixels at once) | 20-40% | Medium |
| P2 | Memoization for repeated coordinates | Variable | Low |

---

## Part 5: Performance Regression Testing

### Benchmark Script

Create `benchmark.sh`:
```bash
#!/bin/bash
ITERATIONS=${1:-5}
RESULTS_FILE="benchmark_results.csv"

echo "timestamp,run,generation_ms,thermal_ms,hydraulic_ms,render_ms" > $RESULTS_FILE

for i in $(seq 1 $ITERATIONS); do
    echo "Run $i of $ITERATIONS..."
    # Add --benchmark flag to your app that outputs timing CSV
    ./build/worldgen --benchmark >> $RESULTS_FILE
done

echo "Results saved to $RESULTS_FILE"
```

### Continuous Monitoring

Add to CI/CD:
```yaml
# .github/workflows/benchmark.yml
- name: Run Benchmarks
  run: |
    ./build.sh
    ./benchmark.sh 3
    python3 scripts/check_regression.py benchmark_results.csv
```

---

## Part 6: Quick Commands Reference

```bash
# Full profiling session
./build.sh && instruments -t "Time Profiler" ./build/worldgen

# Memory check
leaks --atExit -- ./build/worldgen

# Thread sanitizer build
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..

# Address sanitizer build
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address" ..

# Generate flame graph (requires flamegraph.pl)
sample worldgen 30 -file sample.txt
stackcollapse-sample.pl sample.txt | flamegraph.pl > flame.svg
```

---

## Optimization Roadmap

### Phase 1: Quick Wins (1-2 days)
- [ ] Add Profiler instrumentation to key functions
- [ ] Cache hydraulic erosion height calculations
- [ ] Implement adaptive droplet count based on preview mode
- [ ] Thread-local buffers for thermal erosion

### Phase 2: Medium Effort (1 week)
- [ ] Dirty rectangle tracking for renderer
- [ ] Spatial partitioning for parallel hydraulic erosion
- [ ] SIMD vectorization for gradient calculations

### Phase 3: Architectural (2+ weeks)
- [ ] GPU compute shaders for erosion
- [ ] Chunked LOD system with async generation
- [ ] Streaming terrain for large worlds

---

## Expected Results After Phase 1

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Hydraulic step time | ~500ms | ~400ms | 20% |
| Thermal step time | ~50ms | ~35ms | 30% |
| Render time | ~20ms | ~15ms | 25% |
| **Total generation** | ~30s | ~22s | **27%** |

*Times are estimates for 2048×2048 terrain on M1 Mac*
