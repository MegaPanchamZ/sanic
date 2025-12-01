#include "PCGFramework.h"
#include "VulkanRenderer.h"
#include "LandscapeSystem.h"
#include "FoliageSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <queue>
#include <fstream>

namespace Kinetic {

//------------------------------------------------------------------------------
// PCGSpatialData
//------------------------------------------------------------------------------

void PCGSpatialData::updateBounds() {
    boundsMin = glm::vec3(std::numeric_limits<float>::max());
    boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    
    for (const auto& point : points) {
        boundsMin = glm::min(boundsMin, point.position);
        boundsMax = glm::max(boundsMax, point.position);
    }
    
    spatialIndexDirty = true;
}

void PCGSpatialData::clear() {
    points.clear();
    boundsMin = glm::vec3(std::numeric_limits<float>::max());
    boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    spatialIndexDirty = true;
}

void PCGSpatialData::append(const PCGSpatialData& other) {
    points.insert(points.end(), other.points.begin(), other.points.end());
    boundsMin = glm::min(boundsMin, other.boundsMin);
    boundsMax = glm::max(boundsMax, other.boundsMax);
    spatialIndexDirty = true;
}

void PCGSpatialData::buildSpatialIndex(float cellSize) const {
    if (!spatialIndexDirty && gridCellSize == cellSize) return;
    
    gridCellSize = cellSize;
    glm::vec3 size = boundsMax - boundsMin;
    gridDimensions = glm::ivec3(
        std::max(1, static_cast<int>(std::ceil(size.x / cellSize))),
        std::max(1, static_cast<int>(std::ceil(size.y / cellSize))),
        std::max(1, static_cast<int>(std::ceil(size.z / cellSize)))
    );
    
    // Build grid (simple spatial hashing for now)
    spatialGrid.clear();
    spatialGrid.resize(gridDimensions.x * gridDimensions.y * gridDimensions.z);
    
    // Just store point indices - a production implementation would use proper 
    // cell lists or a spatial hash map
    
    spatialIndexDirty = false;
}

std::vector<uint32_t> PCGSpatialData::queryRadius(const glm::vec3& center, float radius) const {
    std::vector<uint32_t> result;
    float radiusSq = radius * radius;
    
    for (uint32_t i = 0; i < points.size(); i++) {
        float distSq = glm::length2(points[i].position - center);
        if (distSq <= radiusSq) {
            result.push_back(i);
        }
    }
    
    return result;
}

std::vector<uint32_t> PCGSpatialData::queryBox(const glm::vec3& min, const glm::vec3& max) const {
    std::vector<uint32_t> result;
    
    for (uint32_t i = 0; i < points.size(); i++) {
        const auto& pos = points[i].position;
        if (pos.x >= min.x && pos.x <= max.x &&
            pos.y >= min.y && pos.y <= max.y &&
            pos.z >= min.z && pos.z <= max.z) {
            result.push_back(i);
        }
    }
    
    return result;
}

//------------------------------------------------------------------------------
// PCGNode
//------------------------------------------------------------------------------

void PCGNode::setSetting(const std::string& name, const Setting::value_type& value) {
    settings_[name] = {name, value};
}

const PCGNode::Setting* PCGNode::getSetting(const std::string& name) const {
    auto it = settings_.find(name);
    return it != settings_.end() ? &it->second : nullptr;
}

//------------------------------------------------------------------------------
// Surface Sampler Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGSurfaceSamplerNode::getInputPins() const {
    return {
        {"Landscape", PCGPinType::Landscape, true, {}}
    };
}

std::vector<PCGPin> PCGSurfaceSamplerNode::getOutputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

bool PCGSurfaceSamplerNode::execute(PCGContext& context,
                                     const std::vector<PCGData>& inputs,
                                     std::vector<PCGData>& outputs) {
    context.seedRNG(nodeId);
    
    PCGSpatialData output;
    
    // Get landscape data if available
    const PCGLandscapeData* landscape = nullptr;
    if (!inputs.empty() && std::holds_alternative<PCGLandscapeData>(inputs[0])) {
        landscape = &std::get<PCGLandscapeData>(inputs[0]);
    }
    
    // Calculate sampling bounds
    glm::vec3 boundsMin = landscape ? landscape->boundsMin : context.worldBoundsMin;
    glm::vec3 boundsMax = landscape ? landscape->boundsMax : context.worldBoundsMax;
    
    float area = (boundsMax.x - boundsMin.x) * (boundsMax.z - boundsMin.z);
    uint32_t numPoints = static_cast<uint32_t>(area * pointsPerSquareMeter_);
    
    output.points.reserve(numPoints);
    
    for (uint32_t i = 0; i < numPoints; i++) {
        PCGPoint point;
        
        // Random XZ position
        point.position.x = context.randomFloat(boundsMin.x, boundsMax.x);
        point.position.z = context.randomFloat(boundsMin.z, boundsMax.z);
        
        // Get height from landscape
        if (landscape && landscape->heightQuery) {
            point.position.y = landscape->heightQuery(glm::vec2(point.position.x, point.position.z));
        } else if (context.landscape) {
            point.position.y = context.landscape->getHeightAt(point.position.x, point.position.z);
        } else {
            point.position.y = 0.0f;
        }
        
        // Height filter
        if (point.position.y < minHeight_ || point.position.y > maxHeight_) {
            continue;
        }
        
        // Get normal and slope
        if (landscape && landscape->normalQuery) {
            point.normal = landscape->normalQuery(glm::vec2(point.position.x, point.position.z));
        } else if (context.landscape) {
            point.normal = context.landscape->getNormalAt(point.position.x, point.position.z);
        }
        
        // Calculate slope angle
        float slope = glm::degrees(std::acos(glm::clamp(point.normal.y, 0.0f, 1.0f)));
        if (slope < minSlope_ || slope > maxSlope_) {
            continue;
        }
        
        // Align to normal if requested
        if (alignToNormal_ && point.normal.y < 0.999f) {
            glm::vec3 up(0, 1, 0);
            glm::vec3 axis = glm::normalize(glm::cross(up, point.normal));
            float angle = std::acos(glm::dot(up, point.normal));
            point.rotation = glm::angleAxis(angle, axis);
        }
        
        point.seed = context.getChildSeed(i);
        output.points.push_back(point);
    }
    
    output.updateBounds();
    outputs.push_back(std::move(output));
    
    return true;
}

std::vector<PCGNode::Setting> PCGSurfaceSamplerNode::getDefaultSettings() const {
    return {
        {"PointsPerSquareMeter", 1.0f},
        {"MinHeight", -10000.0f},
        {"MaxHeight", 10000.0f},
        {"MinSlope", 0.0f},
        {"MaxSlope", 90.0f},
        {"AlignToNormal", false}
    };
}

//------------------------------------------------------------------------------
// Spline Sampler Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGSplineSamplerNode::getInputPins() const {
    return {
        {"Spline", PCGPinType::Spline, false, {}},
        {"Landscape", PCGPinType::Landscape, true, {}}
    };
}

std::vector<PCGPin> PCGSplineSamplerNode::getOutputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

bool PCGSplineSamplerNode::execute(PCGContext& context,
                                    const std::vector<PCGData>& inputs,
                                    std::vector<PCGData>& outputs) {
    if (inputs.empty() || !std::holds_alternative<PCGSplineData>(inputs[0])) {
        return false;
    }
    
    const PCGSplineData& spline = std::get<PCGSplineData>(inputs[0]);
    context.seedRNG(nodeId);
    
    PCGSpatialData output;
    
    if (spline.length <= 0 || spline.points.size() < 2) {
        outputs.push_back(std::move(output));
        return true;
    }
    
    uint32_t numPoints = static_cast<uint32_t>(spline.length / spacing_);
    output.points.reserve(numPoints);
    
    for (uint32_t i = 0; i < numPoints; i++) {
        float t = static_cast<float>(i) / static_cast<float>(numPoints);
        
        // Interpolate along spline
        uint32_t segmentCount = static_cast<uint32_t>(spline.points.size()) - 1;
        float segment = t * segmentCount;
        uint32_t segmentIndex = static_cast<uint32_t>(segment);
        float segmentT = segment - segmentIndex;
        
        if (segmentIndex >= segmentCount) {
            segmentIndex = segmentCount - 1;
            segmentT = 1.0f;
        }
        
        const glm::vec3& p0 = spline.points[segmentIndex];
        const glm::vec3& p1 = spline.points[segmentIndex + 1];
        
        PCGPoint point;
        point.position = glm::mix(p0, p1, segmentT);
        
        // Apply offset from spline
        if (std::abs(offsetFromSpline_) > 0.001f) {
            // Calculate perpendicular direction
            glm::vec3 tangent = glm::normalize(p1 - p0);
            glm::vec3 right = glm::normalize(glm::cross(tangent, glm::vec3(0, 1, 0)));
            point.position += right * offsetFromSpline_;
        }
        
        // Project to surface if requested
        if (projectToSurface_ && context.landscape) {
            point.position.y = context.landscape->getHeightAt(point.position.x, point.position.z);
            point.normal = context.landscape->getNormalAt(point.position.x, point.position.z);
        }
        
        point.seed = context.getChildSeed(i);
        output.points.push_back(point);
    }
    
    output.updateBounds();
    outputs.push_back(std::move(output));
    
    return true;
}

std::vector<PCGNode::Setting> PCGSplineSamplerNode::getDefaultSettings() const {
    return {
        {"Spacing", 10.0f},
        {"ProjectToSurface", true},
        {"OffsetFromSpline", 0.0f}
    };
}

//------------------------------------------------------------------------------
// Volume Sampler Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGVolumeSamplerNode::getInputPins() const {
    return {};
}

std::vector<PCGPin> PCGVolumeSamplerNode::getOutputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

bool PCGVolumeSamplerNode::execute(PCGContext& context,
                                    const std::vector<PCGData>& inputs,
                                    std::vector<PCGData>& outputs) {
    context.seedRNG(nodeId);
    
    PCGSpatialData output;
    
    glm::vec3 size = context.worldBoundsMax - context.worldBoundsMin;
    float volume = size.x * size.y * size.z;
    uint32_t numPoints = static_cast<uint32_t>(volume * density_);
    
    if (usePoissonDisk_) {
        // Poisson disk sampling for better distribution
        float minDist = std::pow(volume / numPoints, 1.0f / 3.0f);
        
        std::vector<PCGPoint> activeList;
        
        // Start with one random point
        PCGPoint first;
        first.position = glm::vec3(
            context.randomFloat(context.worldBoundsMin.x, context.worldBoundsMax.x),
            context.randomFloat(context.worldBoundsMin.y, context.worldBoundsMax.y),
            context.randomFloat(context.worldBoundsMin.z, context.worldBoundsMax.z)
        );
        first.seed = context.getChildSeed(0);
        activeList.push_back(first);
        output.points.push_back(first);
        
        uint32_t maxAttempts = 30;
        
        while (!activeList.empty() && output.points.size() < numPoints) {
            uint32_t idx = context.randomInt(0, static_cast<int32_t>(activeList.size()) - 1);
            PCGPoint& active = activeList[idx];
            
            bool found = false;
            for (uint32_t attempt = 0; attempt < maxAttempts; attempt++) {
                // Random point in annulus around active point
                float r = minDist * (1.0f + context.randomFloat());
                float theta = context.randomFloat() * 2.0f * 3.14159f;
                float phi = std::acos(context.randomFloat() * 2.0f - 1.0f);
                
                PCGPoint newPoint;
                newPoint.position = active.position + glm::vec3(
                    r * std::sin(phi) * std::cos(theta),
                    r * std::sin(phi) * std::sin(theta),
                    r * std::cos(phi)
                );
                
                // Check bounds
                if (newPoint.position.x < context.worldBoundsMin.x ||
                    newPoint.position.x > context.worldBoundsMax.x ||
                    newPoint.position.y < context.worldBoundsMin.y ||
                    newPoint.position.y > context.worldBoundsMax.y ||
                    newPoint.position.z < context.worldBoundsMin.z ||
                    newPoint.position.z > context.worldBoundsMax.z) {
                    continue;
                }
                
                // Check distance to all existing points (inefficient but simple)
                bool tooClose = false;
                for (const auto& existing : output.points) {
                    if (glm::length(newPoint.position - existing.position) < minDist) {
                        tooClose = true;
                        break;
                    }
                }
                
                if (!tooClose) {
                    newPoint.seed = context.getChildSeed(static_cast<int32_t>(output.points.size()));
                    activeList.push_back(newPoint);
                    output.points.push_back(newPoint);
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                activeList.erase(activeList.begin() + idx);
            }
        }
    } else {
        // Simple random sampling
        output.points.reserve(numPoints);
        for (uint32_t i = 0; i < numPoints; i++) {
            PCGPoint point;
            point.position = glm::vec3(
                context.randomFloat(context.worldBoundsMin.x, context.worldBoundsMax.x),
                context.randomFloat(context.worldBoundsMin.y, context.worldBoundsMax.y),
                context.randomFloat(context.worldBoundsMin.z, context.worldBoundsMax.z)
            );
            point.seed = context.getChildSeed(i);
            output.points.push_back(point);
        }
    }
    
    output.updateBounds();
    outputs.push_back(std::move(output));
    
    return true;
}

std::vector<PCGNode::Setting> PCGVolumeSamplerNode::getDefaultSettings() const {
    return {
        {"Density", 0.1f},
        {"UsePoissonDisk", true}
    };
}

//------------------------------------------------------------------------------
// Density Filter Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGDensityFilterNode::getInputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

std::vector<PCGPin> PCGDensityFilterNode::getOutputPins() const {
    return {
        {"Kept", PCGPinType::Spatial, false, {}},
        {"Rejected", PCGPinType::Spatial, false, {}}
    };
}

bool PCGDensityFilterNode::execute(PCGContext& context,
                                    const std::vector<PCGData>& inputs,
                                    std::vector<PCGData>& outputs) {
    if (inputs.empty() || !std::holds_alternative<PCGSpatialData>(inputs[0])) {
        return false;
    }
    
    const PCGSpatialData& input = std::get<PCGSpatialData>(inputs[0]);
    context.seedRNG(nodeId);
    
    PCGSpatialData kept, rejected;
    
    for (const auto& point : input.points) {
        // Sample Perlin noise at point position
        float noiseValue = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f / noiseScale_;
        
        for (int octave = 0; octave < noiseOctaves_; octave++) {
            // Simple noise approximation (would use proper Perlin in production)
            float x = point.position.x * frequency;
            float z = point.position.z * frequency;
            float noise = std::sin(x * 12.9898f + z * 78.233f);
            noise = std::abs(noise * 43758.5453f - std::floor(noise * 43758.5453f));
            
            noiseValue += noise * amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        
        noiseValue = (noiseValue + 1.0f) * 0.5f;  // Normalize to 0-1
        
        if (invertDensity_) {
            noiseValue = 1.0f - noiseValue;
        }
        
        bool passes = noiseValue >= densityMin_ && noiseValue <= densityMax_;
        
        if (passes) {
            kept.points.push_back(point);
        } else {
            rejected.points.push_back(point);
        }
    }
    
    kept.updateBounds();
    rejected.updateBounds();
    
    outputs.push_back(std::move(kept));
    outputs.push_back(std::move(rejected));
    
    return true;
}

std::vector<PCGNode::Setting> PCGDensityFilterNode::getDefaultSettings() const {
    return {
        {"DensityMin", 0.0f},
        {"DensityMax", 1.0f},
        {"InvertDensity", false},
        {"NoiseScale", 100.0f},
        {"NoiseOctaves", 4}
    };
}

//------------------------------------------------------------------------------
// Distance Filter Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGDistanceFilterNode::getInputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

std::vector<PCGPin> PCGDistanceFilterNode::getOutputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

bool PCGDistanceFilterNode::execute(PCGContext& context,
                                     const std::vector<PCGData>& inputs,
                                     std::vector<PCGData>& outputs) {
    if (inputs.empty() || !std::holds_alternative<PCGSpatialData>(inputs[0])) {
        return false;
    }
    
    const PCGSpatialData& input = std::get<PCGSpatialData>(inputs[0]);
    context.seedRNG(nodeId);
    
    PCGSpatialData output;
    float minDistSq = minDistance_ * minDistance_;
    
    // Copy points and optionally shuffle
    std::vector<size_t> indices(input.points.size());
    for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
    
    if (mode_ == Mode::Random) {
        for (size_t i = indices.size() - 1; i > 0; i--) {
            size_t j = context.randomInt(0, static_cast<int32_t>(i));
            std::swap(indices[i], indices[j]);
        }
    }
    
    // Process points
    for (size_t idx : indices) {
        const PCGPoint& point = input.points[idx];
        
        bool tooClose = false;
        for (const auto& existing : output.points) {
            if (glm::length2(point.position - existing.position) < minDistSq) {
                tooClose = true;
                break;
            }
        }
        
        if (!tooClose) {
            output.points.push_back(point);
        }
    }
    
    output.updateBounds();
    outputs.push_back(std::move(output));
    
    return true;
}

std::vector<PCGNode::Setting> PCGDistanceFilterNode::getDefaultSettings() const {
    return {
        {"MinDistance", 1.0f},
        {"Mode", 0}  // 0=Random, 1=Priority, 2=Ordered
    };
}

//------------------------------------------------------------------------------
// Bounds Filter Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGBoundsFilterNode::getInputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

std::vector<PCGPin> PCGBoundsFilterNode::getOutputPins() const {
    return {
        {"Inside", PCGPinType::Spatial, false, {}},
        {"Outside", PCGPinType::Spatial, false, {}}
    };
}

bool PCGBoundsFilterNode::execute(PCGContext& context,
                                   const std::vector<PCGData>& inputs,
                                   std::vector<PCGData>& outputs) {
    if (inputs.empty() || !std::holds_alternative<PCGSpatialData>(inputs[0])) {
        return false;
    }
    
    const PCGSpatialData& input = std::get<PCGSpatialData>(inputs[0]);
    
    PCGSpatialData inside, outside;
    
    glm::vec3 checkMin = boundsMin_;
    glm::vec3 checkMax = boundsMax_;
    if (checkMin == glm::vec3(0) && checkMax == glm::vec3(0)) {
        checkMin = context.worldBoundsMin;
        checkMax = context.worldBoundsMax;
    }
    
    for (const auto& point : input.points) {
        bool inBounds = point.position.x >= checkMin.x && point.position.x <= checkMax.x &&
                        point.position.y >= checkMin.y && point.position.y <= checkMax.y &&
                        point.position.z >= checkMin.z && point.position.z <= checkMax.z;
        
        if (invert_) inBounds = !inBounds;
        
        if (inBounds) {
            inside.points.push_back(point);
        } else {
            outside.points.push_back(point);
        }
    }
    
    inside.updateBounds();
    outside.updateBounds();
    
    outputs.push_back(std::move(inside));
    outputs.push_back(std::move(outside));
    
    return true;
}

std::vector<PCGNode::Setting> PCGBoundsFilterNode::getDefaultSettings() const {
    return {
        {"BoundsMin", glm::vec3(0)},
        {"BoundsMax", glm::vec3(0)},
        {"Invert", false}
    };
}

//------------------------------------------------------------------------------
// Layer Filter Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGLayerFilterNode::getInputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}},
        {"Landscape", PCGPinType::Landscape, true, {}}
    };
}

std::vector<PCGPin> PCGLayerFilterNode::getOutputPins() const {
    return {
        {"Kept", PCGPinType::Spatial, false, {}},
        {"Rejected", PCGPinType::Spatial, false, {}}
    };
}

bool PCGLayerFilterNode::execute(PCGContext& context,
                                  const std::vector<PCGData>& inputs,
                                  std::vector<PCGData>& outputs) {
    if (inputs.empty() || !std::holds_alternative<PCGSpatialData>(inputs[0])) {
        return false;
    }
    
    const PCGSpatialData& input = std::get<PCGSpatialData>(inputs[0]);
    const PCGLandscapeData* landscape = nullptr;
    if (inputs.size() > 1 && std::holds_alternative<PCGLandscapeData>(inputs[1])) {
        landscape = &std::get<PCGLandscapeData>(inputs[1]);
    }
    
    PCGSpatialData kept, rejected;
    
    for (const auto& point : input.points) {
        float weight = 0.0f;
        
        if (landscape && landscape->layerWeightQuery) {
            weight = landscape->layerWeightQuery(glm::vec2(point.position.x, point.position.z), layerIndex_);
        } else if (context.landscape) {
            weight = context.landscape->getLayerWeight(point.position.x, point.position.z, layerIndex_);
        }
        
        bool passes = weight >= minWeight_ && weight <= maxWeight_;
        
        if (passes) {
            kept.points.push_back(point);
        } else {
            rejected.points.push_back(point);
        }
    }
    
    kept.updateBounds();
    rejected.updateBounds();
    
    outputs.push_back(std::move(kept));
    outputs.push_back(std::move(rejected));
    
    return true;
}

std::vector<PCGNode::Setting> PCGLayerFilterNode::getDefaultSettings() const {
    return {
        {"LayerIndex", 0},
        {"MinWeight", 0.5f},
        {"MaxWeight", 1.0f}
    };
}

//------------------------------------------------------------------------------
// Transform Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGTransformNode::getInputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

std::vector<PCGPin> PCGTransformNode::getOutputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

bool PCGTransformNode::execute(PCGContext& context,
                                const std::vector<PCGData>& inputs,
                                std::vector<PCGData>& outputs) {
    if (inputs.empty() || !std::holds_alternative<PCGSpatialData>(inputs[0])) {
        return false;
    }
    
    const PCGSpatialData& input = std::get<PCGSpatialData>(inputs[0]);
    context.seedRNG(nodeId);
    
    PCGSpatialData output;
    output.points.reserve(input.points.size());
    
    for (size_t i = 0; i < input.points.size(); i++) {
        PCGPoint point = input.points[i];
        
        // Random offset
        glm::vec3 offset(
            context.randomFloat(offsetMin_.x, offsetMax_.x),
            context.randomFloat(offsetMin_.y, offsetMax_.y),
            context.randomFloat(offsetMin_.z, offsetMax_.z)
        );
        point.position += offset;
        
        // Random rotation
        glm::vec3 eulerRot(
            context.randomFloat(rotationMin_.x, rotationMax_.x),
            context.randomFloat(rotationMin_.y, rotationMax_.y),
            context.randomFloat(rotationMin_.z, rotationMax_.z)
        );
        glm::quat randomRot = glm::quat(glm::radians(eulerRot));
        point.rotation = randomRot * point.rotation;
        
        // Random scale
        if (uniformScale_) {
            float s = context.randomFloat(scaleMin_.x, scaleMax_.x);
            point.scale *= s;
        } else {
            point.scale *= glm::vec3(
                context.randomFloat(scaleMin_.x, scaleMax_.x),
                context.randomFloat(scaleMin_.y, scaleMax_.y),
                context.randomFloat(scaleMin_.z, scaleMax_.z)
            );
        }
        
        output.points.push_back(point);
    }
    
    output.updateBounds();
    outputs.push_back(std::move(output));
    
    return true;
}

std::vector<PCGNode::Setting> PCGTransformNode::getDefaultSettings() const {
    return {
        {"OffsetMin", glm::vec3(0)},
        {"OffsetMax", glm::vec3(0)},
        {"RotationMin", glm::vec3(0)},
        {"RotationMax", glm::vec3(360, 0, 0)},
        {"ScaleMin", glm::vec3(1)},
        {"ScaleMax", glm::vec3(1)},
        {"UniformScale", true}
    };
}

//------------------------------------------------------------------------------
// Project To Surface Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGProjectToSurfaceNode::getInputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}},
        {"Landscape", PCGPinType::Landscape, true, {}}
    };
}

std::vector<PCGPin> PCGProjectToSurfaceNode::getOutputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

bool PCGProjectToSurfaceNode::execute(PCGContext& context,
                                       const std::vector<PCGData>& inputs,
                                       std::vector<PCGData>& outputs) {
    if (inputs.empty() || !std::holds_alternative<PCGSpatialData>(inputs[0])) {
        return false;
    }
    
    const PCGSpatialData& input = std::get<PCGSpatialData>(inputs[0]);
    const PCGLandscapeData* landscape = nullptr;
    if (inputs.size() > 1 && std::holds_alternative<PCGLandscapeData>(inputs[1])) {
        landscape = &std::get<PCGLandscapeData>(inputs[1]);
    }
    
    PCGSpatialData output;
    output.points.reserve(input.points.size());
    
    for (const auto& point : input.points) {
        PCGPoint projected = point;
        
        glm::vec2 xz(point.position.x, point.position.z);
        
        if (landscape && landscape->heightQuery) {
            projected.position.y = landscape->heightQuery(xz) + verticalOffset_;
            if (landscape->normalQuery) {
                projected.normal = landscape->normalQuery(xz);
            }
        } else if (context.landscape) {
            projected.position.y = context.landscape->getHeightAt(xz.x, xz.y) + verticalOffset_;
            projected.normal = context.landscape->getNormalAt(xz.x, xz.y);
        }
        
        if (alignToNormal_ && projected.normal.y < 0.999f) {
            glm::vec3 up(0, 1, 0);
            glm::vec3 axis = glm::normalize(glm::cross(up, projected.normal));
            float angle = std::acos(glm::dot(up, projected.normal));
            projected.rotation = glm::angleAxis(angle, axis) * projected.rotation;
        }
        
        output.points.push_back(projected);
    }
    
    output.updateBounds();
    outputs.push_back(std::move(output));
    
    return true;
}

std::vector<PCGNode::Setting> PCGProjectToSurfaceNode::getDefaultSettings() const {
    return {
        {"VerticalOffset", 0.0f},
        {"AlignToNormal", false}
    };
}

//------------------------------------------------------------------------------
// Static Mesh Spawner Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGStaticMeshSpawnerNode::getInputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

std::vector<PCGPin> PCGStaticMeshSpawnerNode::getOutputPins() const {
    return {};  // Spawner is a terminal node
}

bool PCGStaticMeshSpawnerNode::execute(PCGContext& context,
                                        const std::vector<PCGData>& inputs,
                                        std::vector<PCGData>& outputs) {
    if (inputs.empty() || !std::holds_alternative<PCGSpatialData>(inputs[0])) {
        return false;
    }
    
    const PCGSpatialData& input = std::get<PCGSpatialData>(inputs[0]);
    context.seedRNG(nodeId);
    
    if (meshPaths_.empty()) {
        return true;  // Nothing to spawn
    }
    
    // Calculate total weight
    float totalWeight = 0.0f;
    for (float w : meshWeights_) totalWeight += w;
    if (totalWeight <= 0.0f) totalWeight = 1.0f;
    
    for (const auto& point : input.points) {
        // Select mesh based on weights
        float r = context.randomFloat(0.0f, totalWeight);
        size_t meshIndex = 0;
        float acc = 0.0f;
        for (size_t i = 0; i < meshWeights_.size(); i++) {
            acc += meshWeights_[i];
            if (r <= acc) {
                meshIndex = i;
                break;
            }
        }
        
        // Would spawn mesh here using the renderer
        // renderer->spawnStaticMesh(meshPaths_[meshIndex], point.position, point.rotation, point.scale);
    }
    
    return true;
}

std::vector<PCGNode::Setting> PCGStaticMeshSpawnerNode::getDefaultSettings() const {
    return {
        {"UseInstancedRendering", true},
        {"CullDistance", 10000.0f}
    };
}

void PCGStaticMeshSpawnerNode::setMeshAssets(const std::vector<std::string>& meshPaths) {
    meshPaths_ = meshPaths;
    meshWeights_.resize(meshPaths.size(), 1.0f);
}

//------------------------------------------------------------------------------
// Foliage Spawner Node
//------------------------------------------------------------------------------

std::vector<PCGPin> PCGFoliageSpawnerNode::getInputPins() const {
    return {
        {"Points", PCGPinType::Spatial, false, {}}
    };
}

std::vector<PCGPin> PCGFoliageSpawnerNode::getOutputPins() const {
    return {};
}

bool PCGFoliageSpawnerNode::execute(PCGContext& context,
                                     const std::vector<PCGData>& inputs,
                                     std::vector<PCGData>& outputs) {
    if (inputs.empty() || !std::holds_alternative<PCGSpatialData>(inputs[0])) {
        return false;
    }
    
    const PCGSpatialData& input = std::get<PCGSpatialData>(inputs[0]);
    
    if (!context.foliage || foliageTypeId_ == 0) {
        return true;  // Nothing to spawn
    }
    
    for (const auto& point : input.points) {
        // Add foliage instance
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), point.position);
        transform = transform * glm::mat4_cast(point.rotation);
        transform = glm::scale(transform, point.scale);
        
        context.foliage->addInstance(foliageTypeId_, transform);
    }
    
    return true;
}

std::vector<PCGNode::Setting> PCGFoliageSpawnerNode::getDefaultSettings() const {
    return {
        {"FoliageTypeId", 0}
    };
}

void PCGFoliageSpawnerNode::setFoliageType(uint32_t foliageTypeId) {
    foliageTypeId_ = foliageTypeId;
}

//------------------------------------------------------------------------------
// PCG Graph
//------------------------------------------------------------------------------

PCGGraph::PCGGraph() {}

PCGGraph::~PCGGraph() {}

uint32_t PCGGraph::addNode(std::unique_ptr<PCGNode> node) {
    uint32_t id = nextNodeId_++;
    node->nodeId = id;
    nodes_.push_back(std::move(node));
    orderDirty_ = true;
    return id;
}

void PCGGraph::removeNode(uint32_t nodeId) {
    // Remove connections involving this node
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                       [nodeId](const PCGConnection& c) {
                           return c.sourceNode == nodeId || c.targetNode == nodeId;
                       }),
        connections_.end()
    );
    
    // Remove node
    nodes_.erase(
        std::remove_if(nodes_.begin(), nodes_.end(),
                       [nodeId](const auto& n) { return n->nodeId == nodeId; }),
        nodes_.end()
    );
    
    orderDirty_ = true;
}

PCGNode* PCGGraph::getNode(uint32_t nodeId) {
    for (auto& node : nodes_) {
        if (node->nodeId == nodeId) {
            return node.get();
        }
    }
    return nullptr;
}

bool PCGGraph::connect(uint32_t sourceNode, uint32_t sourcePin,
                        uint32_t targetNode, uint32_t targetPin) {
    // Validate nodes exist
    PCGNode* src = getNode(sourceNode);
    PCGNode* dst = getNode(targetNode);
    if (!src || !dst) return false;
    
    // Validate pins
    auto srcPins = src->getOutputPins();
    auto dstPins = dst->getInputPins();
    if (sourcePin >= srcPins.size() || targetPin >= dstPins.size()) return false;
    
    // Check type compatibility
    if (srcPins[sourcePin].type != dstPins[targetPin].type &&
        dstPins[targetPin].type != PCGPinType::Any) {
        return false;
    }
    
    // Remove existing connection to target pin
    disconnect(targetNode, targetPin);
    
    // Add connection
    connections_.push_back({sourceNode, sourcePin, targetNode, targetPin});
    orderDirty_ = true;
    
    return true;
}

void PCGGraph::disconnect(uint32_t targetNode, uint32_t targetPin) {
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                       [targetNode, targetPin](const PCGConnection& c) {
                           return c.targetNode == targetNode && c.targetPin == targetPin;
                       }),
        connections_.end()
    );
    orderDirty_ = true;
}

void PCGGraph::updateExecutionOrder() {
    if (!orderDirty_) return;
    
    executionOrder_.clear();
    
    // Build dependency graph
    std::unordered_map<uint32_t, std::vector<uint32_t>> deps;  // node -> depends on
    std::unordered_map<uint32_t, int> inDegree;
    
    for (const auto& node : nodes_) {
        deps[node->nodeId] = {};
        inDegree[node->nodeId] = 0;
    }
    
    for (const auto& conn : connections_) {
        deps[conn.targetNode].push_back(conn.sourceNode);
        inDegree[conn.targetNode]++;
    }
    
    // Topological sort (Kahn's algorithm)
    std::queue<uint32_t> queue;
    for (const auto& [id, degree] : inDegree) {
        if (degree == 0) {
            queue.push(id);
        }
    }
    
    while (!queue.empty()) {
        uint32_t nodeId = queue.front();
        queue.pop();
        executionOrder_.push_back(nodeId);
        
        for (const auto& conn : connections_) {
            if (conn.sourceNode == nodeId) {
                if (--inDegree[conn.targetNode] == 0) {
                    queue.push(conn.targetNode);
                }
            }
        }
    }
    
    orderDirty_ = false;
}

bool PCGGraph::execute(PCGContext& context) {
    updateExecutionOrder();
    
    // Store outputs for each node
    std::unordered_map<uint32_t, std::vector<PCGData>> nodeOutputs;
    
    for (uint32_t nodeId : executionOrder_) {
        PCGNode* node = getNode(nodeId);
        if (!node) continue;
        
        // Gather inputs
        std::vector<PCGData> inputs;
        auto inputPins = node->getInputPins();
        inputs.resize(inputPins.size());
        
        for (const auto& conn : connections_) {
            if (conn.targetNode == nodeId) {
                auto& srcOutputs = nodeOutputs[conn.sourceNode];
                if (conn.sourcePin < srcOutputs.size()) {
                    inputs[conn.targetPin] = srcOutputs[conn.sourcePin];
                }
            }
        }
        
        // Execute node
        std::vector<PCGData> outputs;
        if (!node->execute(context, inputs, outputs)) {
            return false;
        }
        
        nodeOutputs[nodeId] = std::move(outputs);
    }
    
    return true;
}

bool PCGGraph::executePartial(PCGContext& context, const std::vector<uint32_t>& nodeIds) {
    // Execute only specified nodes and their dependencies
    // (simplified - would need proper dependency tracking in production)
    return execute(context);
}

bool PCGGraph::save(const std::string& path) const {
    // Serialization would go here
    return true;
}

bool PCGGraph::load(const std::string& path) {
    // Deserialization would go here
    return true;
}

bool PCGGraph::validate() const {
    return getValidationErrors().empty();
}

std::vector<std::string> PCGGraph::getValidationErrors() const {
    std::vector<std::string> errors;
    
    // Check for cycles
    // Check for unconnected required pins
    // etc.
    
    return errors;
}

//------------------------------------------------------------------------------
// PCG Framework
//------------------------------------------------------------------------------

PCGFramework::PCGFramework() {
    registerDefaultNodes();
}

PCGFramework::~PCGFramework() {
    shutdown();
}

bool PCGFramework::initialize(VulkanRenderer* renderer) {
    m_renderer = renderer;
    return true;
}

void PCGFramework::shutdown() {
    m_graphs.clear();
    m_renderer = nullptr;
}

void PCGFramework::setLandscapeSystem(LandscapeSystem* landscape) {
    m_landscape = landscape;
}

void PCGFramework::setFoliageSystem(FoliageSystem* foliage) {
    m_foliage = foliage;
}

uint32_t PCGFramework::createGraph(const std::string& name) {
    auto graph = std::make_unique<PCGGraph>();
    uint32_t id = m_nextGraphId++;
    m_graphs[id] = std::move(graph);
    return id;
}

void PCGFramework::destroyGraph(uint32_t graphId) {
    m_graphs.erase(graphId);
}

PCGGraph* PCGFramework::getGraph(uint32_t graphId) {
    auto it = m_graphs.find(graphId);
    return it != m_graphs.end() ? it->second.get() : nullptr;
}

bool PCGFramework::executeGraph(uint32_t graphId, const PCGContext& baseContext) {
    PCGGraph* graph = getGraph(graphId);
    if (!graph) return false;
    
    PCGContext context = baseContext;
    context.landscape = m_landscape;
    context.foliage = m_foliage;
    
    return graph->execute(context);
}

bool PCGFramework::executeGraphInBounds(uint32_t graphId, const glm::vec3& boundsMin,
                                         const glm::vec3& boundsMax, int32_t seed) {
    PCGContext context;
    context.seed = seed;
    context.worldBoundsMin = boundsMin;
    context.worldBoundsMax = boundsMax;
    
    return executeGraph(graphId, context);
}

void PCGFramework::registerDefaultNodes() {
    m_nodeFactories["Surface Sampler"] = []() { return std::make_unique<PCGSurfaceSamplerNode>(); };
    m_nodeFactories["Spline Sampler"] = []() { return std::make_unique<PCGSplineSamplerNode>(); };
    m_nodeFactories["Volume Sampler"] = []() { return std::make_unique<PCGVolumeSamplerNode>(); };
    m_nodeFactories["Density Filter"] = []() { return std::make_unique<PCGDensityFilterNode>(); };
    m_nodeFactories["Distance Filter"] = []() { return std::make_unique<PCGDistanceFilterNode>(); };
    m_nodeFactories["Bounds Filter"] = []() { return std::make_unique<PCGBoundsFilterNode>(); };
    m_nodeFactories["Layer Filter"] = []() { return std::make_unique<PCGLayerFilterNode>(); };
    m_nodeFactories["Transform"] = []() { return std::make_unique<PCGTransformNode>(); };
    m_nodeFactories["Project To Surface"] = []() { return std::make_unique<PCGProjectToSurfaceNode>(); };
    m_nodeFactories["Static Mesh Spawner"] = []() { return std::make_unique<PCGStaticMeshSpawnerNode>(); };
    m_nodeFactories["Foliage Spawner"] = []() { return std::make_unique<PCGFoliageSpawnerNode>(); };
}

std::unique_ptr<PCGNode> PCGFramework::createNode(const std::string& typeName) {
    auto it = m_nodeFactories.find(typeName);
    if (it != m_nodeFactories.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> PCGFramework::getAvailableNodeTypes() const {
    std::vector<std::string> types;
    for (const auto& [name, factory] : m_nodeFactories) {
        types.push_back(name);
    }
    return types;
}

void PCGFramework::generateForest(const glm::vec3& boundsMin, const glm::vec3& boundsMax,
                                   int32_t seed, float density) {
    uint32_t graphId = createGraph("Forest");
    PCGGraph* graph = getGraph(graphId);
    
    // Create sampler
    auto sampler = createNode("Surface Sampler");
    sampler->setSetting("PointsPerSquareMeter", density);
    uint32_t samplerId = graph->addNode(std::move(sampler));
    
    // Create density filter
    auto filter = createNode("Density Filter");
    filter->setSetting("NoiseScale", 50.0f);
    uint32_t filterId = graph->addNode(std::move(filter));
    graph->connect(samplerId, 0, filterId, 0);
    
    // Create transform
    auto transform = createNode("Transform");
    transform->setSetting("ScaleMin", glm::vec3(0.8f));
    transform->setSetting("ScaleMax", glm::vec3(1.2f));
    uint32_t transformId = graph->addNode(std::move(transform));
    graph->connect(filterId, 0, transformId, 0);
    
    // Create spawner
    auto spawner = createNode("Foliage Spawner");
    uint32_t spawnerId = graph->addNode(std::move(spawner));
    graph->connect(transformId, 0, spawnerId, 0);
    
    // Execute
    executeGraphInBounds(graphId, boundsMin, boundsMax, seed);
    
    // Cleanup
    destroyGraph(graphId);
}

void PCGFramework::generateRocks(const glm::vec3& boundsMin, const glm::vec3& boundsMax,
                                  int32_t seed, float density) {
    // Similar to forest but with rock meshes
    uint32_t graphId = createGraph("Rocks");
    PCGGraph* graph = getGraph(graphId);
    
    auto sampler = createNode("Surface Sampler");
    sampler->setSetting("PointsPerSquareMeter", density * 0.1f);
    sampler->setSetting("MinSlope", 10.0f);  // Rocks on slopes
    uint32_t samplerId = graph->addNode(std::move(sampler));
    
    auto distance = createNode("Distance Filter");
    distance->setSetting("MinDistance", 5.0f);
    uint32_t distanceId = graph->addNode(std::move(distance));
    graph->connect(samplerId, 0, distanceId, 0);
    
    auto transform = createNode("Transform");
    transform->setSetting("ScaleMin", glm::vec3(0.5f));
    transform->setSetting("ScaleMax", glm::vec3(2.0f));
    transform->setSetting("RotationMax", glm::vec3(360, 360, 360));
    uint32_t transformId = graph->addNode(std::move(transform));
    graph->connect(distanceId, 0, transformId, 0);
    
    auto spawner = createNode("Static Mesh Spawner");
    uint32_t spawnerId = graph->addNode(std::move(spawner));
    graph->connect(transformId, 0, spawnerId, 0);
    
    executeGraphInBounds(graphId, boundsMin, boundsMax, seed);
    destroyGraph(graphId);
}

void PCGFramework::populateSpline(const PCGSplineData& spline, int32_t seed) {
    // Populate along spline (roads, paths, etc.)
}

void PCGFramework::drawDebugUI() {
    // ImGui debug interface
}

} // namespace Kinetic
