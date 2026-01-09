#include "simulation/HydraulicErosion.hpp"
#include "utils/Math.hpp"
#include <cmath>

namespace worldgen {

HydraulicErosion::HydraulicErosion() {
    precomputeErosionBrush();
}

HydraulicErosion::HydraulicErosion(const HydraulicParams& params)
    : m_params(params)
{
    precomputeErosionBrush();
}

void HydraulicErosion::initialize(TerrainData& terrain) {
    m_terrain = &terrain;
    m_iterations = 0;
    precomputeErosionBrush();
}

void HydraulicErosion::step() {
    if (!m_terrain) return;

    for (int i = 0; i < m_params.dropletsPerStep; ++i) {
        simulateDroplet();
    }
    ++m_iterations;
}

void HydraulicErosion::reset() {
    m_iterations = 0;
}

void HydraulicErosion::setParams(const HydraulicParams& params) {
    m_params = params;
    precomputeErosionBrush();
}

void HydraulicErosion::precomputeErosionBrush() {
    if (!m_terrain) return;

    const int w = static_cast<int>(m_terrain->width());
    const int h = static_cast<int>(m_terrain->heightDim());

    m_erosionBrushIndices.resize(w * h);
    m_erosionBrushWeights.resize(w * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int centerIndex = y * w + x;

            float weightSum = 0.0f;
            std::vector<int> indices;
            std::vector<float> weights;

            for (int dy = -m_params.erosionRadius; dy <= m_params.erosionRadius; ++dy) {
                for (int dx = -m_params.erosionRadius; dx <= m_params.erosionRadius; ++dx) {
                    int nx = x + dx;
                    int ny = y + dy;

                    if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                        float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                        if (dist <= m_params.erosionRadius) {
                            float weight = std::max(0.0f, m_params.erosionRadius - dist);
                            indices.push_back(ny * w + nx);
                            weights.push_back(weight);
                            weightSum += weight;
                        }
                    }
                }
            }

            // Normalize weights
            if (weightSum > 0) {
                for (auto& w : weights) {
                    w /= weightSum;
                }
            }

            m_erosionBrushIndices[centerIndex] = std::move(indices);
            m_erosionBrushWeights[centerIndex] = std::move(weights);
        }
    }
}

HydraulicErosion::HeightAndGradient HydraulicErosion::calculateHeightAndGradient(float posX, float posY) {
    int coordX = static_cast<int>(posX);
    int coordY = static_cast<int>(posY);

    float x = posX - coordX;
    float y = posY - coordY;

    const int w = static_cast<int>(m_terrain->width());
    int x1 = std::min(coordX + 1, w - 1);
    int y1 = std::min(coordY + 1, static_cast<int>(m_terrain->heightDim()) - 1);

    float h00 = m_terrain->height->get(coordX, coordY);
    float h10 = m_terrain->height->get(x1, coordY);
    float h01 = m_terrain->height->get(coordX, y1);
    float h11 = m_terrain->height->get(x1, y1);

    float gradientX = (h10 - h00) * (1 - y) + (h11 - h01) * y;
    float gradientY = (h01 - h00) * (1 - x) + (h11 - h10) * x;

    float height = h00 * (1 - x) * (1 - y) + h10 * x * (1 - y) + h01 * (1 - x) * y + h11 * x * y;

    return {height, gradientX, gradientY};
}

void HydraulicErosion::simulateDroplet() {
    const int w = static_cast<int>(m_terrain->width());
    const int h = static_cast<int>(m_terrain->heightDim());

    float posX = m_random.nextFloat(0, static_cast<float>(w - 2));
    float posY = m_random.nextFloat(0, static_cast<float>(h - 2));
    float dirX = 0.0f, dirY = 0.0f;
    float speed = m_params.initialSpeed;
    float water = m_params.initialWater;
    float sediment = 0.0f;

    for (int lifetime = 0; lifetime < m_params.maxDropletLifetime; ++lifetime) {
        int nodeX = static_cast<int>(posX);
        int nodeY = static_cast<int>(posY);
        int dropletIndex = nodeY * w + nodeX;

        float cellOffsetX = posX - nodeX;
        float cellOffsetY = posY - nodeY;

        auto [height, gradX, gradY] = calculateHeightAndGradient(posX, posY);

        dirX = dirX * m_params.inertia - gradX * (1 - m_params.inertia);
        dirY = dirY * m_params.inertia - gradY * (1 - m_params.inertia);

        float len = std::sqrt(dirX * dirX + dirY * dirY);
        if (len > 0.0001f) {
            dirX /= len;
            dirY /= len;
        }

        float newPosX = posX + dirX;
        float newPosY = posY + dirY;

        if (newPosX < 0 || newPosX >= w - 1 || newPosY < 0 || newPosY >= h - 1) {
            break;
        }

        float newHeight = calculateHeightAndGradient(newPosX, newPosY).height;
        float deltaHeight = newHeight - height;

        float capacity = std::max(-deltaHeight * speed * water * m_params.sedimentCapacity,
                                   m_params.minSedimentCapacity);

        if (sediment > capacity || deltaHeight > 0) {
            float amountToDeposit = (deltaHeight > 0)
                ? std::min(deltaHeight, sediment)
                : (sediment - capacity) * m_params.depositSpeed;

            sediment -= amountToDeposit;

            // Deposit using bilinear interpolation
            m_terrain->height->at(nodeX, nodeY) += amountToDeposit * (1 - cellOffsetX) * (1 - cellOffsetY);
            m_terrain->height->at(nodeX + 1, nodeY) += amountToDeposit * cellOffsetX * (1 - cellOffsetY);
            m_terrain->height->at(nodeX, nodeY + 1) += amountToDeposit * (1 - cellOffsetX) * cellOffsetY;
            m_terrain->height->at(nodeX + 1, nodeY + 1) += amountToDeposit * cellOffsetX * cellOffsetY;
        } else {
            float amountToErode = std::min((capacity - sediment) * m_params.erodeSpeed, -deltaHeight);

            // Erode using brush
            const auto& brushIndices = m_erosionBrushIndices[dropletIndex];
            const auto& brushWeights = m_erosionBrushWeights[dropletIndex];

            for (size_t i = 0; i < brushIndices.size(); ++i) {
                float erodeAmount = amountToErode * brushWeights[i];
                m_terrain->height->data()[brushIndices[i]] -= erodeAmount;
            }

            sediment += amountToErode;
        }

        posX = newPosX;
        posY = newPosY;
        speed = std::sqrt(std::max(0.0f, speed * speed + deltaHeight * m_params.gravity));
        water *= (1 - m_params.evaporateSpeed);

        if (water < 0.01f) break;
    }
}

} // namespace worldgen
