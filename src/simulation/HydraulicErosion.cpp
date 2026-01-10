#include "simulation/HydraulicErosion.hpp"
#include "utils/Profiler.hpp"
#include <cmath>
#include <algorithm>

namespace worldgen {

HydraulicErosion::HydraulicErosion() {
    // Brush precomputed on initialize when terrain is available
}

HydraulicErosion::HydraulicErosion(const HydraulicParams& params)
    : m_params(params)
{
}

void HydraulicErosion::initialize(TerrainData& terrain) {
    m_terrain = &terrain;
    m_iterations = 0;
    m_width = static_cast<int>(terrain.width());
    m_height = static_cast<int>(terrain.heightDim());
    precomputeErosionBrush();
}

void HydraulicErosion::step() {
    PROFILE_SCOPE("HydraulicErosion::step");
    if (!m_terrain) return;

    const int chunksPerSide = m_params.parallelChunks;
    const int dropletsPerChunk = m_params.dropletsPerStep / (chunksPerSide * chunksPerSide);

    // Process chunks in a checkerboard pattern for parallelism
    // Even pass: chunks where (cx + cy) % 2 == 0
    // Odd pass: chunks where (cx + cy) % 2 == 1
    // This ensures non-adjacent chunks are processed, reducing contention

    for (int pass = 0; pass < 2; ++pass) {
        #ifdef WORLDGEN_USE_OPENMP
        #pragma omp parallel
        {
            // Each thread gets its own RNG seeded differently
            Random threadRng(static_cast<uint32_t>(m_iterations * 1000 + omp_get_thread_num() * 12345));

            #pragma omp for collapse(2) schedule(dynamic)
            for (int cy = 0; cy < chunksPerSide; ++cy) {
                for (int cx = 0; cx < chunksPerSide; ++cx) {
                    if ((cx + cy) % 2 == pass) {
                        for (int i = 0; i < dropletsPerChunk; ++i) {
                            simulateDroplet(threadRng, cx, cy, chunksPerSide);
                        }
                    }
                }
            }
        }
        #else
        // Sequential fallback
        for (int cy = 0; cy < chunksPerSide; ++cy) {
            for (int cx = 0; cx < chunksPerSide; ++cx) {
                if ((cx + cy) % 2 == pass) {
                    for (int i = 0; i < dropletsPerChunk; ++i) {
                        simulateDroplet(m_random, cx, cy, chunksPerSide);
                    }
                }
            }
        }
        #endif
    }

    ++m_iterations;
}

void HydraulicErosion::reset() {
    m_iterations = 0;
}

void HydraulicErosion::setParams(const HydraulicParams& params) {
    bool needsRebuild = (params.erosionRadius != m_params.erosionRadius);
    m_params = params;
    if (needsRebuild && m_terrain) {
        precomputeErosionBrush();
    }
}

void HydraulicErosion::precomputeErosionBrush() {
    if (!m_terrain) return;

    const int w = m_width;
    const int h = m_height;
    const int radius = m_params.erosionRadius;

    m_erosionBrush.resize(w * h);

    #ifdef WORLDGEN_USE_OPENMP
    #pragma omp parallel for
    #endif
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int centerIndex = y * w + x;
            auto& brush = m_erosionBrush[centerIndex];

            brush.indices.clear();
            brush.weights.clear();

            float weightSum = 0.0f;

            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    int nx = x + dx;
                    int ny = y + dy;

                    if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                        float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                        if (dist <= radius) {
                            float weight = std::max(0.0f, static_cast<float>(radius) - dist);
                            brush.indices.push_back(ny * w + nx);
                            brush.weights.push_back(weight);
                            weightSum += weight;
                        }
                    }
                }
            }

            // Normalize weights
            if (weightSum > 0.0f) {
                float invSum = 1.0f / weightSum;
                for (auto& wt : brush.weights) {
                    wt *= invSum;
                }
            }
        }
    }
}

inline void HydraulicErosion::getCornerHeights(int x, int y, float& h00, float& h10, float& h01, float& h11) const {
    const float* data = m_terrain->height->data();
    const int w = m_width;

    int x1 = std::min(x + 1, m_width - 1);
    int y1 = std::min(y + 1, m_height - 1);

    h00 = data[y * w + x];
    h10 = data[y * w + x1];
    h01 = data[y1 * w + x];
    h11 = data[y1 * w + x1];
}

HydraulicErosion::HeightAndGradient HydraulicErosion::calculateHeightAndGradient(float posX, float posY) const {
    int coordX = static_cast<int>(posX);
    int coordY = static_cast<int>(posY);

    float x = posX - coordX;
    float y = posY - coordY;

    float h00, h10, h01, h11;
    getCornerHeights(coordX, coordY, h00, h10, h01, h11);

    float gradientX = (h10 - h00) * (1.0f - y) + (h11 - h01) * y;
    float gradientY = (h01 - h00) * (1.0f - x) + (h11 - h10) * x;

    float height = h00 * (1.0f - x) * (1.0f - y) +
                   h10 * x * (1.0f - y) +
                   h01 * (1.0f - x) * y +
                   h11 * x * y;

    return {height, gradientX, gradientY};
}

void HydraulicErosion::simulateDroplet(Random& rng, int chunkX, int chunkY, int chunksPerSide) {
    const int w = m_width;
    const int h = m_height;

    // Calculate chunk boundaries
    const int chunkW = (w - 2) / chunksPerSide;
    const int chunkH = (h - 2) / chunksPerSide;

    const float startX = static_cast<float>(chunkX * chunkW + 1);
    const float startY = static_cast<float>(chunkY * chunkH + 1);
    const float endX = static_cast<float>(std::min((chunkX + 1) * chunkW, w - 2));
    const float endY = static_cast<float>(std::min((chunkY + 1) * chunkH, h - 2));

    // Start droplet within chunk
    float posX = rng.nextFloat(startX, endX);
    float posY = rng.nextFloat(startY, endY);
    float dirX = 0.0f, dirY = 0.0f;
    float speed = m_params.initialSpeed;
    float water = m_params.initialWater;
    float sediment = 0.0f;

    // Cache params locally for faster access
    const float inertia = m_params.inertia;
    const float oneMinusInertia = 1.0f - inertia;
    const float sedimentCapacity = m_params.sedimentCapacity;
    const float minSedimentCapacity = m_params.minSedimentCapacity;
    const float depositSpeed = m_params.depositSpeed;
    const float erodeSpeed = m_params.erodeSpeed;
    const float evaporateSpeed = m_params.evaporateSpeed;
    const float gravity = m_params.gravity;
    const float evapMultiplier = 1.0f - evaporateSpeed;
    const float seaLevel = m_params.seaLevel;
    const float minWaterForLake = m_params.minWaterForTermination;

    // Get water data for lake detection
    const float* waterData = m_terrain->water ? m_terrain->water->data() : nullptr;

    // Calculate initial height and gradient (will be reused in next iteration)
    auto current = calculateHeightAndGradient(posX, posY);

    float* heightData = m_terrain->height->data();

    for (int lifetime = 0; lifetime < m_params.maxDropletLifetime; ++lifetime) {
        int nodeX = static_cast<int>(posX);
        int nodeY = static_cast<int>(posY);
        int dropletIndex = nodeY * w + nodeX;

        // === TERMINATION CONDITIONS ===

        // 1. Reached sea level - droplet joins the ocean
        // Don't deposit sediment here - ocean currents carry it away
        if (current.height <= seaLevel) {
            break;
        }

        // 2. Reached a lake/water body - droplet joins existing water
        // Don't deposit at lakes either - sediment settles on lake bottom (not terrain)
        if (waterData && waterData[dropletIndex] > minWaterForLake) {
            break;
        }

        // 3. Droplet is stuck in a pit (no gradient, very slow)
        // Only deposit sediment in inland depressions, not at water bodies
        if (speed < 0.001f && lifetime > 10) {
            heightData[dropletIndex] += sediment * 0.5f;  // Partial deposit
            break;
        }

        float cellOffsetX = posX - nodeX;
        float cellOffsetY = posY - nodeY;

        // Update direction based on gradient (reusing cached values)
        dirX = dirX * inertia - current.gradientX * oneMinusInertia;
        dirY = dirY * inertia - current.gradientY * oneMinusInertia;

        // Normalize direction
        float len = std::sqrt(dirX * dirX + dirY * dirY);
        if (len > 0.0001f) {
            float invLen = 1.0f / len;
            dirX *= invLen;
            dirY *= invLen;
        }

        float newPosX = posX + dirX;
        float newPosY = posY + dirY;

        // 4. Check bounds - droplet leaves terrain
        if (newPosX < 0 || newPosX >= w - 1 || newPosY < 0 || newPosY >= h - 1) {
            break;
        }

        // Calculate new height (this becomes current for next iteration)
        auto next = calculateHeightAndGradient(newPosX, newPosY);
        float deltaHeight = next.height - current.height;

        // Calculate sediment capacity
        float capacity = std::max(-deltaHeight * speed * water * sedimentCapacity,
                                  minSedimentCapacity);

        if (sediment > capacity || deltaHeight > 0) {
            // Deposit sediment
            float amountToDeposit = (deltaHeight > 0)
                ? std::min(deltaHeight, sediment)
                : (sediment - capacity) * depositSpeed;

            sediment -= amountToDeposit;

            // Concentrated deposition to reduce ridge artifacts
            // depositConcentration controls how much goes to single nearest cell vs bilinear spread
            const float concentration = m_params.depositConcentration;

            // Find nearest cell based on offset
            int nearestOffsetX = (cellOffsetX < 0.5f) ? 0 : 1;
            int nearestOffsetY = (cellOffsetY < 0.5f) ? 0 : 1;
            int nearestIdx = (nodeY + nearestOffsetY) * w + (nodeX + nearestOffsetX);

            // Concentrated portion goes to single nearest cell
            float concentratedAmount = amountToDeposit * concentration;
            heightData[nearestIdx] += concentratedAmount;

            // Remaining portion spread using bilinear interpolation
            float spreadAmount = amountToDeposit * (1.0f - concentration);
            float w00 = (1.0f - cellOffsetX) * (1.0f - cellOffsetY);
            float w10 = cellOffsetX * (1.0f - cellOffsetY);
            float w01 = (1.0f - cellOffsetX) * cellOffsetY;
            float w11 = cellOffsetX * cellOffsetY;

            heightData[nodeY * w + nodeX] += spreadAmount * w00;
            heightData[nodeY * w + nodeX + 1] += spreadAmount * w10;
            heightData[(nodeY + 1) * w + nodeX] += spreadAmount * w01;
            heightData[(nodeY + 1) * w + nodeX + 1] += spreadAmount * w11;
        } else {
            // Erode terrain
            float amountToErode = std::min((capacity - sediment) * erodeSpeed, -deltaHeight);

            // Apply erosion using precomputed brush
            const auto& brush = m_erosionBrush[dropletIndex];
            const size_t brushSize = brush.indices.size();
            for (size_t i = 0; i < brushSize; ++i) {
                heightData[brush.indices[i]] -= amountToErode * brush.weights[i];
            }

            sediment += amountToErode;
        }

        // Update position and reuse height calculation
        posX = newPosX;
        posY = newPosY;
        current = next;  // Reuse calculated height for next iteration

        // Update speed and water (very slow evaporation)
        speed = std::sqrt(std::max(0.0f, speed * speed + deltaHeight * gravity));
        water *= evapMultiplier;

        // 5. Only terminate from evaporation if water is nearly gone
        if (water < 0.001f) break;
    }
}

void HydraulicErosion::simulateDropletBatch(int /*startIdx*/, int count, Random& rng) {
    // Simplified batch simulation for sequential execution
    for (int i = 0; i < count; ++i) {
        simulateDroplet(rng, 0, 0, 1);
    }
}

} // namespace worldgen
