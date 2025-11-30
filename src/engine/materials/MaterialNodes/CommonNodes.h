/**
 * CommonNodes.h
 * 
 * Standard material nodes for constants, textures, math operations, and utilities.
 */

#pragma once

#include "../MaterialNode.h"

namespace Sanic {

// ============================================================================
// CONSTANT NODES
// ============================================================================

/**
 * Scalar constant node (single float value)
 */
class ScalarNode : public MaterialNode {
public:
    ScalarNode();
    std::string getName() const override { return "Scalar"; }
    std::string getCategory() const override { return "Constants"; }
    std::string getDescription() const override { return "A constant scalar (float) value"; }
    glm::vec4 getColor() const override { return glm::vec4(0.3f, 0.5f, 0.3f, 1.0f); }
    std::string generateCode(MaterialCompiler& c) const override;
    bool supportsPreview() const override { return true; }
    
    float value = 0.0f;
};

/**
 * Vector constant node (vec2, vec3, or vec4)
 */
class VectorNode : public MaterialNode {
public:
    VectorNode();
    std::string getName() const override { return "Vector"; }
    std::string getCategory() const override { return "Constants"; }
    std::string getDescription() const override { return "A constant vector value (vec4)"; }
    glm::vec4 getColor() const override { return glm::vec4(0.3f, 0.5f, 0.3f, 1.0f); }
    std::string generateCode(MaterialCompiler& c) const override;
    bool supportsPreview() const override { return true; }
    
    glm::vec4 value = glm::vec4(0.0f);
};

/**
 * Color constant node (RGB or RGBA)
 */
class ColorNode : public MaterialNode {
public:
    ColorNode();
    std::string getName() const override { return "Color"; }
    std::string getCategory() const override { return "Constants"; }
    std::string getDescription() const override { return "A constant color value"; }
    glm::vec4 getColor() const override { return glm::vec4(0.8f, 0.2f, 0.2f, 1.0f); }
    std::string generateCode(MaterialCompiler& c) const override;
    bool supportsPreview() const override { return true; }
    
    glm::vec3 color = glm::vec3(1.0f);
    float alpha = 1.0f;
};

// ============================================================================
// TEXTURE NODES
// ============================================================================

/**
 * 2D Texture sample node
 */
class TextureSampleNode : public MaterialNode {
public:
    TextureSampleNode();
    std::string getName() const override { return "Texture Sample"; }
    std::string getCategory() const override { return "Textures"; }
    std::string getDescription() const override { return "Sample a 2D texture"; }
    glm::vec4 getColor() const override { return glm::vec4(0.2f, 0.6f, 0.2f, 1.0f); }
    float getWidth() const override { return 220.0f; }
    std::string generateCode(MaterialCompiler& c) const override;
    bool supportsPreview() const override { return true; }
    
    std::string texturePath;  // Default texture path
    uint32_t textureSlot = 0; // Bindless texture slot
    bool useSRGB = true;      // Gamma correction
};

/**
 * Texture coordinates node
 */
class TexCoordNode : public MaterialNode {
public:
    TexCoordNode();
    std::string getName() const override { return "TexCoord"; }
    std::string getCategory() const override { return "Textures"; }
    std::string getDescription() const override { return "Texture coordinates (UV)"; }
    std::string generateCode(MaterialCompiler& c) const override;
    
    uint32_t uvChannel = 0;
};

/**
 * Texture coordinate transformation
 */
class TexCoordTransformNode : public MaterialNode {
public:
    TexCoordTransformNode();
    std::string getName() const override { return "UV Transform"; }
    std::string getCategory() const override { return "Textures"; }
    std::string getDescription() const override { return "Transform texture coordinates (tile, offset, rotate)"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Parallax occlusion mapping node
 */
class ParallaxOcclusionNode : public MaterialNode {
public:
    ParallaxOcclusionNode();
    std::string getName() const override { return "Parallax Occlusion"; }
    std::string getCategory() const override { return "Textures"; }
    std::string getDescription() const override { return "Parallax occlusion mapping"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

// ============================================================================
// MATH NODES
// ============================================================================

/**
 * Add two values
 */
class AddNode : public MaterialNode {
public:
    AddNode();
    std::string getName() const override { return "Add"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Add two values (A + B)"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Subtract two values
 */
class SubtractNode : public MaterialNode {
public:
    SubtractNode();
    std::string getName() const override { return "Subtract"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Subtract two values (A - B)"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Multiply two values
 */
class MultiplyNode : public MaterialNode {
public:
    MultiplyNode();
    std::string getName() const override { return "Multiply"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Multiply two values (A * B)"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Divide two values
 */
class DivideNode : public MaterialNode {
public:
    DivideNode();
    std::string getName() const override { return "Divide"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Divide two values (A / B)"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Linear interpolation
 */
class LerpNode : public MaterialNode {
public:
    LerpNode();
    std::string getName() const override { return "Lerp"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Linear interpolation between A and B"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Clamp value to range
 */
class ClampNode : public MaterialNode {
public:
    ClampNode();
    std::string getName() const override { return "Clamp"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Clamp value between min and max"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Saturate (clamp 0-1)
 */
class SaturateNode : public MaterialNode {
public:
    SaturateNode();
    std::string getName() const override { return "Saturate"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Clamp value to 0-1 range"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * One minus value
 */
class OneMinusNode : public MaterialNode {
public:
    OneMinusNode();
    std::string getName() const override { return "One Minus"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Returns 1 - input"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Absolute value
 */
class AbsNode : public MaterialNode {
public:
    AbsNode();
    std::string getName() const override { return "Abs"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Absolute value"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Power function
 */
class PowerNode : public MaterialNode {
public:
    PowerNode();
    std::string getName() const override { return "Power"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Base raised to exponent power"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Square root
 */
class SqrtNode : public MaterialNode {
public:
    SqrtNode();
    std::string getName() const override { return "Sqrt"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Square root"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Normalize vector
 */
class NormalizeNode : public MaterialNode {
public:
    NormalizeNode();
    std::string getName() const override { return "Normalize"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Normalize vector to unit length"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Dot product
 */
class DotProductNode : public MaterialNode {
public:
    DotProductNode();
    std::string getName() const override { return "Dot Product"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Dot product of two vectors"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Cross product
 */
class CrossProductNode : public MaterialNode {
public:
    CrossProductNode();
    std::string getName() const override { return "Cross Product"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Cross product of two 3D vectors"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Reflect vector
 */
class ReflectNode : public MaterialNode {
public:
    ReflectNode();
    std::string getName() const override { return "Reflect"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Reflect vector around normal"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Fresnel effect
 */
class FresnelNode : public MaterialNode {
public:
    FresnelNode();
    std::string getName() const override { return "Fresnel"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Fresnel effect based on view angle"; }
    glm::vec4 getColor() const override { return glm::vec4(0.4f, 0.4f, 0.7f, 1.0f); }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Sine function
 */
class SinNode : public MaterialNode {
public:
    SinNode();
    std::string getName() const override { return "Sin"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Sine of input (radians)"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Cosine function
 */
class CosNode : public MaterialNode {
public:
    CosNode();
    std::string getName() const override { return "Cos"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Cosine of input (radians)"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Floor function
 */
class FloorNode : public MaterialNode {
public:
    FloorNode();
    std::string getName() const override { return "Floor"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Floor of input value"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Frac function
 */
class FracNode : public MaterialNode {
public:
    FracNode();
    std::string getName() const override { return "Frac"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Fractional part of input value"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Min function
 */
class MinNode : public MaterialNode {
public:
    MinNode();
    std::string getName() const override { return "Min"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Minimum of two values"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Max function
 */
class MaxNode : public MaterialNode {
public:
    MaxNode();
    std::string getName() const override { return "Max"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Maximum of two values"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Smooth step
 */
class SmoothStepNode : public MaterialNode {
public:
    SmoothStepNode();
    std::string getName() const override { return "Smooth Step"; }
    std::string getCategory() const override { return "Math"; }
    std::string getDescription() const override { return "Hermite interpolation between 0 and 1"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

// ============================================================================
// UTILITY NODES
// ============================================================================

/**
 * Time node (for animations)
 */
class TimeNode : public MaterialNode {
public:
    TimeNode();
    std::string getName() const override { return "Time"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "Time values for animation"; }
    glm::vec4 getColor() const override { return glm::vec4(0.6f, 0.4f, 0.2f, 1.0f); }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * World position node
 */
class WorldPositionNode : public MaterialNode {
public:
    WorldPositionNode();
    std::string getName() const override { return "World Position"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "World space position of the pixel"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * World normal node
 */
class WorldNormalNode : public MaterialNode {
public:
    WorldNormalNode();
    std::string getName() const override { return "World Normal"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "World space normal vector"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * View direction node
 */
class ViewDirectionNode : public MaterialNode {
public:
    ViewDirectionNode();
    std::string getName() const override { return "View Direction"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "Direction from pixel to camera"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Camera position node
 */
class CameraPositionNode : public MaterialNode {
public:
    CameraPositionNode();
    std::string getName() const override { return "Camera Position"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "World space camera position"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Screen position node
 */
class ScreenPositionNode : public MaterialNode {
public:
    ScreenPositionNode();
    std::string getName() const override { return "Screen Position"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "Screen space position of the pixel"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Vertex color node
 */
class VertexColorNode : public MaterialNode {
public:
    VertexColorNode();
    std::string getName() const override { return "Vertex Color"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "Per-vertex color attribute"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Append vectors node
 */
class AppendNode : public MaterialNode {
public:
    AppendNode();
    std::string getName() const override { return "Append"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "Combine values into a vector"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

/**
 * Component mask (swizzle) node
 */
class ComponentMaskNode : public MaterialNode {
public:
    ComponentMaskNode();
    std::string getName() const override { return "Component Mask"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "Extract/reorder vector components"; }
    std::string generateCode(MaterialCompiler& c) const override;
    
    bool r = true, g = true, b = true, a = true;
};

/**
 * If/branch node
 */
class IfNode : public MaterialNode {
public:
    IfNode();
    std::string getName() const override { return "If"; }
    std::string getCategory() const override { return "Utility"; }
    std::string getDescription() const override { return "Conditional branch (A > B ? True : False)"; }
    std::string generateCode(MaterialCompiler& c) const override;
};

// ============================================================================
// OUTPUT NODE (Special - one per material)
// ============================================================================

/**
 * Material output node - final outputs for the material
 */
class MaterialOutputNode : public MaterialNode {
public:
    MaterialOutputNode();
    std::string getName() const override { return "Material Output"; }
    std::string getCategory() const override { return "Output"; }
    std::string getDescription() const override { return "Final material outputs (PBR)"; }
    glm::vec4 getColor() const override { return glm::vec4(0.8f, 0.4f, 0.1f, 1.0f); }
    float getWidth() const override { return 240.0f; }
    std::string generateCode(MaterialCompiler& c) const override;
    
    // This node cannot be deleted
    bool validate(std::string& error) const override { return true; }
};

} // namespace Sanic
