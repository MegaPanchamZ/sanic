/**
 * CommonNodes.cpp
 * 
 * Implementation of standard material nodes.
 */

#include "CommonNodes.h"
#include "../MaterialCompiler.h"

namespace Sanic {

// ============================================================================
// CONSTANT NODES
// ============================================================================

ScalarNode::ScalarNode() {
    addOutputPin("Value", MaterialValueType::Float);
}

std::string ScalarNode::generateCode(MaterialCompiler& c) const {
    std::string varName = c.generateUniqueVar("scalar");
    c.addLine("float " + varName + " = " + std::to_string(value) + ";");
    return varName;
}

VectorNode::VectorNode() {
    addOutputPin("RGBA", MaterialValueType::Float4);
    addOutputPin("RGB", MaterialValueType::Float3);
    addOutputPin("R", MaterialValueType::Float);
    addOutputPin("G", MaterialValueType::Float);
    addOutputPin("B", MaterialValueType::Float);
    addOutputPin("A", MaterialValueType::Float);
}

std::string VectorNode::generateCode(MaterialCompiler& c) const {
    std::string varName = c.generateUniqueVar("vector");
    c.addLine("vec4 " + varName + " = vec4(" +
              std::to_string(value.x) + ", " +
              std::to_string(value.y) + ", " +
              std::to_string(value.z) + ", " +
              std::to_string(value.w) + ");");
    
    // Register component outputs
    c.registerOutput(id, "RGB", varName + ".rgb");
    c.registerOutput(id, "R", varName + ".r");
    c.registerOutput(id, "G", varName + ".g");
    c.registerOutput(id, "B", varName + ".b");
    c.registerOutput(id, "A", varName + ".a");
    
    return varName;
}

ColorNode::ColorNode() {
    addOutputPin("RGB", MaterialValueType::Float3);
    addOutputPin("R", MaterialValueType::Float);
    addOutputPin("G", MaterialValueType::Float);
    addOutputPin("B", MaterialValueType::Float);
    addOutputPin("Alpha", MaterialValueType::Float);
}

std::string ColorNode::generateCode(MaterialCompiler& c) const {
    std::string varName = c.generateUniqueVar("color");
    c.addLine("vec3 " + varName + " = vec3(" +
              std::to_string(color.r) + ", " +
              std::to_string(color.g) + ", " +
              std::to_string(color.b) + ");");
    
    c.registerOutput(id, "R", varName + ".r");
    c.registerOutput(id, "G", varName + ".g");
    c.registerOutput(id, "B", varName + ".b");
    c.registerOutput(id, "Alpha", std::to_string(alpha));
    
    return varName;
}

// ============================================================================
// TEXTURE NODES
// ============================================================================

TextureSampleNode::TextureSampleNode() {
    addInputPin("UV", MaterialValueType::Float2, true);
    addOutputPin("RGBA", MaterialValueType::Float4);
    addOutputPin("RGB", MaterialValueType::Float3);
    addOutputPin("R", MaterialValueType::Float);
    addOutputPin("G", MaterialValueType::Float);
    addOutputPin("B", MaterialValueType::Float);
    addOutputPin("A", MaterialValueType::Float);
}

std::string TextureSampleNode::generateCode(MaterialCompiler& c) const {
    // Get UV input or use default
    std::string uv = c.getInputValue(this, "UV", "v_TexCoord");
    
    // Register this texture for binding
    std::string samplerName = c.registerTexture(texturePath, textureSlot, useSRGB);
    
    std::string varName = c.generateUniqueVar("texSample");
    c.addLine("vec4 " + varName + " = texture(" + samplerName + ", " + uv + ");");
    
    // Register component outputs
    c.registerOutput(id, "RGB", varName + ".rgb");
    c.registerOutput(id, "R", varName + ".r");
    c.registerOutput(id, "G", varName + ".g");
    c.registerOutput(id, "B", varName + ".b");
    c.registerOutput(id, "A", varName + ".a");
    
    return varName;
}

TexCoordNode::TexCoordNode() {
    addOutputPin("UV", MaterialValueType::Float2);
    addOutputPin("U", MaterialValueType::Float);
    addOutputPin("V", MaterialValueType::Float);
}

std::string TexCoordNode::generateCode(MaterialCompiler& c) const {
    std::string uvName = "v_TexCoord";
    if (uvChannel > 0) {
        uvName = "v_TexCoord" + std::to_string(uvChannel);
    }
    
    c.registerOutput(id, "U", uvName + ".x");
    c.registerOutput(id, "V", uvName + ".y");
    
    return uvName;
}

TexCoordTransformNode::TexCoordTransformNode() {
    addInputPin("UV", MaterialValueType::Float2, true);
    addInputPin("Tiling", MaterialValueType::Float2, true);
    addInputPin("Offset", MaterialValueType::Float2, true);
    addInputPin("Rotation", MaterialValueType::Float, true);
    addOutputPin("UV", MaterialValueType::Float2);
}

std::string TexCoordTransformNode::generateCode(MaterialCompiler& c) const {
    std::string uv = c.getInputValue(this, "UV", "v_TexCoord");
    std::string tiling = c.getInputValue(this, "Tiling", "vec2(1.0)");
    std::string offset = c.getInputValue(this, "Offset", "vec2(0.0)");
    std::string rotation = c.getInputValue(this, "Rotation", "0.0");
    
    std::string varName = c.generateUniqueVar("uvTransform");
    
    // Apply tiling and offset, then rotation
    c.addLine("vec2 " + varName + "_centered = " + uv + " - vec2(0.5);");
    c.addLine("float " + varName + "_cos = cos(" + rotation + ");");
    c.addLine("float " + varName + "_sin = sin(" + rotation + ");");
    c.addLine("vec2 " + varName + " = vec2(");
    c.addLine("    " + varName + "_centered.x * " + varName + "_cos - " + varName + "_centered.y * " + varName + "_sin,");
    c.addLine("    " + varName + "_centered.x * " + varName + "_sin + " + varName + "_centered.y * " + varName + "_cos");
    c.addLine(") + vec2(0.5);");
    c.addLine(varName + " = " + varName + " * " + tiling + " + " + offset + ";");
    
    return varName;
}

ParallaxOcclusionNode::ParallaxOcclusionNode() {
    addInputPin("UV", MaterialValueType::Float2, true);
    addInputPin("HeightMap", MaterialValueType::Float, false);
    addInputPin("Scale", MaterialValueType::Float, true);
    addInputPin("Steps", MaterialValueType::Float, true);
    addOutputPin("UV", MaterialValueType::Float2);
    addOutputPin("Depth", MaterialValueType::Float);
}

std::string ParallaxOcclusionNode::generateCode(MaterialCompiler& c) const {
    std::string uv = c.getInputValue(this, "UV", "v_TexCoord");
    std::string scale = c.getInputValue(this, "Scale", "0.05");
    std::string steps = c.getInputValue(this, "Steps", "16.0");
    
    std::string varName = c.generateUniqueVar("pom");
    
    // Simplified parallax occlusion mapping
    c.addLine("// Parallax Occlusion Mapping");
    c.addLine("vec3 " + varName + "_viewDir = normalize(v_TangentViewPos - v_TangentFragPos);");
    c.addLine("float " + varName + "_layerDepth = 1.0 / " + steps + ";");
    c.addLine("float " + varName + "_currentLayerDepth = 0.0;");
    c.addLine("vec2 " + varName + "_deltaUV = " + varName + "_viewDir.xy * " + scale + " / " + steps + ";");
    c.addLine("vec2 " + varName + "_uv = " + uv + ";");
    c.addLine("float " + varName + "_depth = 0.0;");
    c.addLine("for(int i = 0; i < int(" + steps + "); i++) {");
    c.addLine("    " + varName + "_currentLayerDepth += " + varName + "_layerDepth;");
    c.addLine("    " + varName + "_uv -= " + varName + "_deltaUV;");
    c.addLine("    " + varName + "_depth = " + c.getInputValue(this, "HeightMap", "0.0") + ";");
    c.addLine("    if(" + varName + "_depth < " + varName + "_currentLayerDepth) break;");
    c.addLine("}");
    
    c.registerOutput(id, "Depth", varName + "_depth");
    
    return varName + "_uv";
}

// ============================================================================
// MATH NODES
// ============================================================================

AddNode::AddNode() {
    addInputPin("A", MaterialValueType::Float4, true);
    addInputPin("B", MaterialValueType::Float4, true);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string AddNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "0.0");
    std::string b = c.getInputValue(this, "B", "0.0");
    
    std::string varName = c.generateUniqueVar("add");
    std::string resultType = c.inferResultType(a, b);
    c.addLine(resultType + " " + varName + " = " + a + " + " + b + ";");
    return varName;
}

SubtractNode::SubtractNode() {
    addInputPin("A", MaterialValueType::Float4, true);
    addInputPin("B", MaterialValueType::Float4, true);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string SubtractNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "0.0");
    std::string b = c.getInputValue(this, "B", "0.0");
    
    std::string varName = c.generateUniqueVar("sub");
    std::string resultType = c.inferResultType(a, b);
    c.addLine(resultType + " " + varName + " = " + a + " - " + b + ";");
    return varName;
}

MultiplyNode::MultiplyNode() {
    addInputPin("A", MaterialValueType::Float4, true);
    addInputPin("B", MaterialValueType::Float4, true);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string MultiplyNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "1.0");
    std::string b = c.getInputValue(this, "B", "1.0");
    
    std::string varName = c.generateUniqueVar("mul");
    std::string resultType = c.inferResultType(a, b);
    c.addLine(resultType + " " + varName + " = " + a + " * " + b + ";");
    return varName;
}

DivideNode::DivideNode() {
    addInputPin("A", MaterialValueType::Float4, true);
    addInputPin("B", MaterialValueType::Float4, true);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string DivideNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "1.0");
    std::string b = c.getInputValue(this, "B", "1.0");
    
    std::string varName = c.generateUniqueVar("div");
    std::string resultType = c.inferResultType(a, b);
    c.addLine(resultType + " " + varName + " = " + a + " / max(" + b + ", 0.0001);"); // Avoid division by zero
    return varName;
}

LerpNode::LerpNode() {
    addInputPin("A", MaterialValueType::Float4, true);
    addInputPin("B", MaterialValueType::Float4, true);
    addInputPin("Alpha", MaterialValueType::Float, true);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string LerpNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "0.0");
    std::string b = c.getInputValue(this, "B", "1.0");
    std::string alpha = c.getInputValue(this, "Alpha", "0.5");
    
    std::string varName = c.generateUniqueVar("lerp");
    std::string resultType = c.inferResultType(a, b);
    c.addLine(resultType + " " + varName + " = mix(" + a + ", " + b + ", " + alpha + ");");
    return varName;
}

ClampNode::ClampNode() {
    addInputPin("Value", MaterialValueType::Float4, false);
    addInputPin("Min", MaterialValueType::Float, true);
    addInputPin("Max", MaterialValueType::Float, true);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string ClampNode::generateCode(MaterialCompiler& c) const {
    std::string value = c.getInputValue(this, "Value", "0.0");
    std::string minVal = c.getInputValue(this, "Min", "0.0");
    std::string maxVal = c.getInputValue(this, "Max", "1.0");
    
    std::string varName = c.generateUniqueVar("clamped");
    std::string resultType = c.getValueType(value);
    c.addLine(resultType + " " + varName + " = clamp(" + value + ", " + minVal + ", " + maxVal + ");");
    return varName;
}

SaturateNode::SaturateNode() {
    addInputPin("Value", MaterialValueType::Float4, false);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string SaturateNode::generateCode(MaterialCompiler& c) const {
    std::string value = c.getInputValue(this, "Value", "0.0");
    
    std::string varName = c.generateUniqueVar("saturate");
    std::string resultType = c.getValueType(value);
    c.addLine(resultType + " " + varName + " = clamp(" + value + ", 0.0, 1.0);");
    return varName;
}

OneMinusNode::OneMinusNode() {
    addInputPin("Value", MaterialValueType::Float4, false);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string OneMinusNode::generateCode(MaterialCompiler& c) const {
    std::string value = c.getInputValue(this, "Value", "0.0");
    
    std::string varName = c.generateUniqueVar("oneMinus");
    std::string resultType = c.getValueType(value);
    c.addLine(resultType + " " + varName + " = 1.0 - " + value + ";");
    return varName;
}

AbsNode::AbsNode() {
    addInputPin("Value", MaterialValueType::Float4, false);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string AbsNode::generateCode(MaterialCompiler& c) const {
    std::string value = c.getInputValue(this, "Value", "0.0");
    
    std::string varName = c.generateUniqueVar("absVal");
    std::string resultType = c.getValueType(value);
    c.addLine(resultType + " " + varName + " = abs(" + value + ");");
    return varName;
}

PowerNode::PowerNode() {
    addInputPin("Base", MaterialValueType::Float4, false);
    addInputPin("Exponent", MaterialValueType::Float, true);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string PowerNode::generateCode(MaterialCompiler& c) const {
    std::string base = c.getInputValue(this, "Base", "2.0");
    std::string exp = c.getInputValue(this, "Exponent", "2.0");
    
    std::string varName = c.generateUniqueVar("power");
    std::string resultType = c.getValueType(base);
    c.addLine(resultType + " " + varName + " = pow(max(" + base + ", 0.0), " + exp + ");");
    return varName;
}

SqrtNode::SqrtNode() {
    addInputPin("Value", MaterialValueType::Float4, false);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string SqrtNode::generateCode(MaterialCompiler& c) const {
    std::string value = c.getInputValue(this, "Value", "1.0");
    
    std::string varName = c.generateUniqueVar("sqrtVal");
    std::string resultType = c.getValueType(value);
    c.addLine(resultType + " " + varName + " = sqrt(max(" + value + ", 0.0));");
    return varName;
}

NormalizeNode::NormalizeNode() {
    addInputPin("Vector", MaterialValueType::Float3, false);
    addOutputPin("Result", MaterialValueType::Float3);
}

std::string NormalizeNode::generateCode(MaterialCompiler& c) const {
    std::string vec = c.getInputValue(this, "Vector", "vec3(0.0, 1.0, 0.0)");
    
    std::string varName = c.generateUniqueVar("normalized");
    c.addLine("vec3 " + varName + " = normalize(" + vec + ");");
    return varName;
}

DotProductNode::DotProductNode() {
    addInputPin("A", MaterialValueType::Float3, false);
    addInputPin("B", MaterialValueType::Float3, false);
    addOutputPin("Result", MaterialValueType::Float);
}

std::string DotProductNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "vec3(1.0, 0.0, 0.0)");
    std::string b = c.getInputValue(this, "B", "vec3(0.0, 1.0, 0.0)");
    
    std::string varName = c.generateUniqueVar("dotProduct");
    c.addLine("float " + varName + " = dot(" + a + ", " + b + ");");
    return varName;
}

CrossProductNode::CrossProductNode() {
    addInputPin("A", MaterialValueType::Float3, false);
    addInputPin("B", MaterialValueType::Float3, false);
    addOutputPin("Result", MaterialValueType::Float3);
}

std::string CrossProductNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "vec3(1.0, 0.0, 0.0)");
    std::string b = c.getInputValue(this, "B", "vec3(0.0, 1.0, 0.0)");
    
    std::string varName = c.generateUniqueVar("crossProduct");
    c.addLine("vec3 " + varName + " = cross(" + a + ", " + b + ");");
    return varName;
}

ReflectNode::ReflectNode() {
    addInputPin("Incident", MaterialValueType::Float3, false);
    addInputPin("Normal", MaterialValueType::Float3, false);
    addOutputPin("Result", MaterialValueType::Float3);
}

std::string ReflectNode::generateCode(MaterialCompiler& c) const {
    std::string incident = c.getInputValue(this, "Incident", "vec3(0.0, -1.0, 0.0)");
    std::string normal = c.getInputValue(this, "Normal", "vec3(0.0, 1.0, 0.0)");
    
    std::string varName = c.generateUniqueVar("reflected");
    c.addLine("vec3 " + varName + " = reflect(" + incident + ", normalize(" + normal + "));");
    return varName;
}

FresnelNode::FresnelNode() {
    addInputPin("Normal", MaterialValueType::Float3, true);
    addInputPin("ViewDir", MaterialValueType::Float3, true);
    addInputPin("Power", MaterialValueType::Float, true);
    addOutputPin("Result", MaterialValueType::Float);
}

std::string FresnelNode::generateCode(MaterialCompiler& c) const {
    std::string normal = c.getInputValue(this, "Normal", "v_WorldNormal");
    std::string viewDir = c.getInputValue(this, "ViewDir", "v_ViewDir");
    std::string power = c.getInputValue(this, "Power", "5.0");
    
    std::string varName = c.generateUniqueVar("fresnel");
    c.addLine("float " + varName + " = pow(1.0 - max(dot(normalize(" + normal + "), normalize(" + viewDir + ")), 0.0), " + power + ");");
    return varName;
}

SinNode::SinNode() {
    addInputPin("Value", MaterialValueType::Float, false);
    addOutputPin("Result", MaterialValueType::Float);
}

std::string SinNode::generateCode(MaterialCompiler& c) const {
    std::string value = c.getInputValue(this, "Value", "0.0");
    
    std::string varName = c.generateUniqueVar("sinVal");
    c.addLine("float " + varName + " = sin(" + value + ");");
    return varName;
}

CosNode::CosNode() {
    addInputPin("Value", MaterialValueType::Float, false);
    addOutputPin("Result", MaterialValueType::Float);
}

std::string CosNode::generateCode(MaterialCompiler& c) const {
    std::string value = c.getInputValue(this, "Value", "0.0");
    
    std::string varName = c.generateUniqueVar("cosVal");
    c.addLine("float " + varName + " = cos(" + value + ");");
    return varName;
}

FloorNode::FloorNode() {
    addInputPin("Value", MaterialValueType::Float4, false);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string FloorNode::generateCode(MaterialCompiler& c) const {
    std::string value = c.getInputValue(this, "Value", "0.0");
    
    std::string varName = c.generateUniqueVar("floorVal");
    std::string resultType = c.getValueType(value);
    c.addLine(resultType + " " + varName + " = floor(" + value + ");");
    return varName;
}

FracNode::FracNode() {
    addInputPin("Value", MaterialValueType::Float4, false);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string FracNode::generateCode(MaterialCompiler& c) const {
    std::string value = c.getInputValue(this, "Value", "0.0");
    
    std::string varName = c.generateUniqueVar("fracVal");
    std::string resultType = c.getValueType(value);
    c.addLine(resultType + " " + varName + " = fract(" + value + ");");
    return varName;
}

MinNode::MinNode() {
    addInputPin("A", MaterialValueType::Float4, false);
    addInputPin("B", MaterialValueType::Float4, false);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string MinNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "0.0");
    std::string b = c.getInputValue(this, "B", "1.0");
    
    std::string varName = c.generateUniqueVar("minVal");
    std::string resultType = c.inferResultType(a, b);
    c.addLine(resultType + " " + varName + " = min(" + a + ", " + b + ");");
    return varName;
}

MaxNode::MaxNode() {
    addInputPin("A", MaterialValueType::Float4, false);
    addInputPin("B", MaterialValueType::Float4, false);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string MaxNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "0.0");
    std::string b = c.getInputValue(this, "B", "1.0");
    
    std::string varName = c.generateUniqueVar("maxVal");
    std::string resultType = c.inferResultType(a, b);
    c.addLine(resultType + " " + varName + " = max(" + a + ", " + b + ");");
    return varName;
}

SmoothStepNode::SmoothStepNode() {
    addInputPin("Edge0", MaterialValueType::Float, true);
    addInputPin("Edge1", MaterialValueType::Float, true);
    addInputPin("X", MaterialValueType::Float, false);
    addOutputPin("Result", MaterialValueType::Float);
}

std::string SmoothStepNode::generateCode(MaterialCompiler& c) const {
    std::string edge0 = c.getInputValue(this, "Edge0", "0.0");
    std::string edge1 = c.getInputValue(this, "Edge1", "1.0");
    std::string x = c.getInputValue(this, "X", "0.5");
    
    std::string varName = c.generateUniqueVar("smoothStep");
    c.addLine("float " + varName + " = smoothstep(" + edge0 + ", " + edge1 + ", " + x + ");");
    return varName;
}

// ============================================================================
// UTILITY NODES
// ============================================================================

TimeNode::TimeNode() {
    addOutputPin("Time", MaterialValueType::Float);
    addOutputPin("SinTime", MaterialValueType::Float);
    addOutputPin("CosTime", MaterialValueType::Float);
    addOutputPin("DeltaTime", MaterialValueType::Float);
}

std::string TimeNode::generateCode(MaterialCompiler& c) const {
    // Time is expected to be provided via uniform
    c.requireUniform("u_Time", "float");
    c.requireUniform("u_DeltaTime", "float");
    
    std::string varName = c.generateUniqueVar("time");
    c.addLine("float " + varName + " = u_Time;");
    
    c.registerOutput(id, "SinTime", "sin(u_Time)");
    c.registerOutput(id, "CosTime", "cos(u_Time)");
    c.registerOutput(id, "DeltaTime", "u_DeltaTime");
    
    return varName;
}

WorldPositionNode::WorldPositionNode() {
    addOutputPin("Position", MaterialValueType::Float3);
    addOutputPin("X", MaterialValueType::Float);
    addOutputPin("Y", MaterialValueType::Float);
    addOutputPin("Z", MaterialValueType::Float);
}

std::string WorldPositionNode::generateCode(MaterialCompiler& c) const {
    c.registerOutput(id, "X", "v_WorldPos.x");
    c.registerOutput(id, "Y", "v_WorldPos.y");
    c.registerOutput(id, "Z", "v_WorldPos.z");
    
    return "v_WorldPos";
}

WorldNormalNode::WorldNormalNode() {
    addOutputPin("Normal", MaterialValueType::Float3);
    addOutputPin("X", MaterialValueType::Float);
    addOutputPin("Y", MaterialValueType::Float);
    addOutputPin("Z", MaterialValueType::Float);
}

std::string WorldNormalNode::generateCode(MaterialCompiler& c) const {
    c.registerOutput(id, "X", "v_WorldNormal.x");
    c.registerOutput(id, "Y", "v_WorldNormal.y");
    c.registerOutput(id, "Z", "v_WorldNormal.z");
    
    return "normalize(v_WorldNormal)";
}

ViewDirectionNode::ViewDirectionNode() {
    addOutputPin("Direction", MaterialValueType::Float3);
}

std::string ViewDirectionNode::generateCode(MaterialCompiler& c) const {
    c.requireUniform("u_CameraPos", "vec3");
    
    std::string varName = c.generateUniqueVar("viewDir");
    c.addLine("vec3 " + varName + " = normalize(u_CameraPos - v_WorldPos);");
    return varName;
}

CameraPositionNode::CameraPositionNode() {
    addOutputPin("Position", MaterialValueType::Float3);
    addOutputPin("X", MaterialValueType::Float);
    addOutputPin("Y", MaterialValueType::Float);
    addOutputPin("Z", MaterialValueType::Float);
}

std::string CameraPositionNode::generateCode(MaterialCompiler& c) const {
    c.requireUniform("u_CameraPos", "vec3");
    
    c.registerOutput(id, "X", "u_CameraPos.x");
    c.registerOutput(id, "Y", "u_CameraPos.y");
    c.registerOutput(id, "Z", "u_CameraPos.z");
    
    return "u_CameraPos";
}

ScreenPositionNode::ScreenPositionNode() {
    addOutputPin("Position", MaterialValueType::Float2);
    addOutputPin("X", MaterialValueType::Float);
    addOutputPin("Y", MaterialValueType::Float);
}

std::string ScreenPositionNode::generateCode(MaterialCompiler& c) const {
    c.registerOutput(id, "X", "gl_FragCoord.x");
    c.registerOutput(id, "Y", "gl_FragCoord.y");
    
    return "gl_FragCoord.xy";
}

VertexColorNode::VertexColorNode() {
    addOutputPin("Color", MaterialValueType::Float4);
    addOutputPin("RGB", MaterialValueType::Float3);
    addOutputPin("R", MaterialValueType::Float);
    addOutputPin("G", MaterialValueType::Float);
    addOutputPin("B", MaterialValueType::Float);
    addOutputPin("A", MaterialValueType::Float);
}

std::string VertexColorNode::generateCode(MaterialCompiler& c) const {
    c.registerOutput(id, "RGB", "v_Color.rgb");
    c.registerOutput(id, "R", "v_Color.r");
    c.registerOutput(id, "G", "v_Color.g");
    c.registerOutput(id, "B", "v_Color.b");
    c.registerOutput(id, "A", "v_Color.a");
    
    return "v_Color";
}

AppendNode::AppendNode() {
    addInputPin("A", MaterialValueType::Float, false);
    addInputPin("B", MaterialValueType::Float, false);
    addInputPin("C", MaterialValueType::Float, true);
    addInputPin("D", MaterialValueType::Float, true);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string AppendNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "0.0");
    std::string b = c.getInputValue(this, "B", "0.0");
    std::string cVal = c.getInputValue(this, "C", "0.0");
    std::string d = c.getInputValue(this, "D", "0.0");
    
    std::string varName = c.generateUniqueVar("appended");
    c.addLine("vec4 " + varName + " = vec4(" + a + ", " + b + ", " + cVal + ", " + d + ");");
    return varName;
}

ComponentMaskNode::ComponentMaskNode() {
    addInputPin("Input", MaterialValueType::Float4, false);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string ComponentMaskNode::generateCode(MaterialCompiler& c) const {
    std::string input = c.getInputValue(this, "Input", "vec4(0.0)");
    
    std::string swizzle;
    if (r) swizzle += "r";
    if (g) swizzle += "g";
    if (b) swizzle += "b";
    if (a) swizzle += "a";
    
    if (swizzle.empty()) swizzle = "r"; // At least one component
    
    std::string varName = c.generateUniqueVar("masked");
    
    if (swizzle.length() == 1) {
        c.addLine("float " + varName + " = " + input + "." + swizzle + ";");
    } else if (swizzle.length() == 2) {
        c.addLine("vec2 " + varName + " = " + input + "." + swizzle + ";");
    } else if (swizzle.length() == 3) {
        c.addLine("vec3 " + varName + " = " + input + "." + swizzle + ";");
    } else {
        c.addLine("vec4 " + varName + " = " + input + "." + swizzle + ";");
    }
    
    return varName;
}

IfNode::IfNode() {
    addInputPin("A", MaterialValueType::Float, false);
    addInputPin("B", MaterialValueType::Float, false);
    addInputPin("A>B", MaterialValueType::Float4, true);
    addInputPin("A=B", MaterialValueType::Float4, true);
    addInputPin("A<B", MaterialValueType::Float4, true);
    addOutputPin("Result", MaterialValueType::Float4);
}

std::string IfNode::generateCode(MaterialCompiler& c) const {
    std::string a = c.getInputValue(this, "A", "0.0");
    std::string b = c.getInputValue(this, "B", "0.0");
    std::string greater = c.getInputValue(this, "A>B", "1.0");
    std::string equal = c.getInputValue(this, "A=B", "0.5");
    std::string less = c.getInputValue(this, "A<B", "0.0");
    
    std::string varName = c.generateUniqueVar("ifResult");
    std::string resultType = c.inferResultType(greater, c.inferResultType(equal, less));
    
    c.addLine(resultType + " " + varName + ";");
    c.addLine("if (" + a + " > " + b + ") " + varName + " = " + greater + ";");
    c.addLine("else if (" + a + " < " + b + ") " + varName + " = " + less + ";");
    c.addLine("else " + varName + " = " + equal + ";");
    
    return varName;
}

// ============================================================================
// OUTPUT NODE
// ============================================================================

MaterialOutputNode::MaterialOutputNode() {
    // Standard PBR inputs
    addInputPin("Base Color", MaterialValueType::Float3, true);
    addInputPin("Metallic", MaterialValueType::Float, true);
    addInputPin("Roughness", MaterialValueType::Float, true);
    addInputPin("Normal", MaterialValueType::Float3, true);
    addInputPin("Emissive", MaterialValueType::Float3, true);
    addInputPin("Ambient Occlusion", MaterialValueType::Float, true);
    addInputPin("Opacity", MaterialValueType::Float, true);
    addInputPin("Opacity Mask", MaterialValueType::Float, true);
    addInputPin("World Position Offset", MaterialValueType::Float3, true);
    
    // Mark as output node (no output pins)
}

std::string MaterialOutputNode::generateCode(MaterialCompiler& c) const {
    // Get all material inputs with defaults
    std::string baseColor = c.getInputValue(this, "Base Color", "vec3(0.5)");
    std::string metallic = c.getInputValue(this, "Metallic", "0.0");
    std::string roughness = c.getInputValue(this, "Roughness", "0.5");
    std::string normal = c.getInputValue(this, "Normal", "v_WorldNormal");
    std::string emissive = c.getInputValue(this, "Emissive", "vec3(0.0)");
    std::string ao = c.getInputValue(this, "Ambient Occlusion", "1.0");
    std::string opacity = c.getInputValue(this, "Opacity", "1.0");
    std::string opacityMask = c.getInputValue(this, "Opacity Mask", "1.0");
    std::string worldPosOffset = c.getInputValue(this, "World Position Offset", "vec3(0.0)");
    
    // Write to GBuffer outputs
    c.addLine("// Material Output");
    c.addLine("out_GBuffer0 = vec4(" + baseColor + ", " + metallic + ");");
    c.addLine("out_GBuffer1 = vec4(encodeNormal(" + normal + "), " + roughness + ", " + ao + ");");
    c.addLine("out_GBuffer2 = vec4(" + emissive + ", 1.0);");
    
    // Handle opacity/alpha
    c.addLine("if (" + opacityMask + " < 0.5) discard;");
    c.addLine("out_Alpha = " + opacity + ";");
    
    // World position offset is handled in vertex shader
    c.setVertexOffset(worldPosOffset);
    
    return ""; // Output node doesn't produce a value
}

// ============================================================================
// NODE REGISTRATION
// ============================================================================

// Register all common nodes with the factory
namespace {
    struct CommonNodesRegistrar {
        CommonNodesRegistrar() {
            // Constants
            REGISTER_MATERIAL_NODE(ScalarNode);
            REGISTER_MATERIAL_NODE(VectorNode);
            REGISTER_MATERIAL_NODE(ColorNode);
            
            // Textures
            REGISTER_MATERIAL_NODE(TextureSampleNode);
            REGISTER_MATERIAL_NODE(TexCoordNode);
            REGISTER_MATERIAL_NODE(TexCoordTransformNode);
            REGISTER_MATERIAL_NODE(ParallaxOcclusionNode);
            
            // Math - Basic
            REGISTER_MATERIAL_NODE(AddNode);
            REGISTER_MATERIAL_NODE(SubtractNode);
            REGISTER_MATERIAL_NODE(MultiplyNode);
            REGISTER_MATERIAL_NODE(DivideNode);
            REGISTER_MATERIAL_NODE(LerpNode);
            REGISTER_MATERIAL_NODE(ClampNode);
            REGISTER_MATERIAL_NODE(SaturateNode);
            REGISTER_MATERIAL_NODE(OneMinusNode);
            REGISTER_MATERIAL_NODE(AbsNode);
            REGISTER_MATERIAL_NODE(PowerNode);
            REGISTER_MATERIAL_NODE(SqrtNode);
            
            // Math - Vector
            REGISTER_MATERIAL_NODE(NormalizeNode);
            REGISTER_MATERIAL_NODE(DotProductNode);
            REGISTER_MATERIAL_NODE(CrossProductNode);
            REGISTER_MATERIAL_NODE(ReflectNode);
            REGISTER_MATERIAL_NODE(FresnelNode);
            
            // Math - Trigonometric
            REGISTER_MATERIAL_NODE(SinNode);
            REGISTER_MATERIAL_NODE(CosNode);
            
            // Math - Rounding
            REGISTER_MATERIAL_NODE(FloorNode);
            REGISTER_MATERIAL_NODE(FracNode);
            REGISTER_MATERIAL_NODE(MinNode);
            REGISTER_MATERIAL_NODE(MaxNode);
            REGISTER_MATERIAL_NODE(SmoothStepNode);
            
            // Utility
            REGISTER_MATERIAL_NODE(TimeNode);
            REGISTER_MATERIAL_NODE(WorldPositionNode);
            REGISTER_MATERIAL_NODE(WorldNormalNode);
            REGISTER_MATERIAL_NODE(ViewDirectionNode);
            REGISTER_MATERIAL_NODE(CameraPositionNode);
            REGISTER_MATERIAL_NODE(ScreenPositionNode);
            REGISTER_MATERIAL_NODE(VertexColorNode);
            REGISTER_MATERIAL_NODE(AppendNode);
            REGISTER_MATERIAL_NODE(ComponentMaskNode);
            REGISTER_MATERIAL_NODE(IfNode);
            
            // Output
            REGISTER_MATERIAL_NODE(MaterialOutputNode);
        }
    };
    
    static CommonNodesRegistrar s_CommonNodesRegistrar;
}

} // namespace Sanic
