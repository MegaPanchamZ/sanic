/**
 * MaterialNode.cpp
 * 
 * Implementation of the material node base class.
 */

#include "MaterialNode.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iostream>

namespace Sanic {

// Type utilities

std::string getGLSLType(MaterialValueType type) {
    switch (type) {
        case MaterialValueType::Float: return "float";
        case MaterialValueType::Float2: return "vec2";
        case MaterialValueType::Float3: return "vec3";
        case MaterialValueType::Float4: return "vec4";
        case MaterialValueType::Int: return "int";
        case MaterialValueType::Bool: return "bool";
        case MaterialValueType::Texture2D: return "sampler2D";
        case MaterialValueType::Texture3D: return "sampler3D";
        case MaterialValueType::TextureCube: return "samplerCube";
        case MaterialValueType::Matrix3: return "mat3";
        case MaterialValueType::Matrix4: return "mat4";
        default: return "float";
    }
}

uint32_t getComponentCount(MaterialValueType type) {
    switch (type) {
        case MaterialValueType::Float:
        case MaterialValueType::Int:
        case MaterialValueType::Bool:
            return 1;
        case MaterialValueType::Float2:
            return 2;
        case MaterialValueType::Float3:
            return 3;
        case MaterialValueType::Float4:
            return 4;
        case MaterialValueType::Matrix3:
            return 9;
        case MaterialValueType::Matrix4:
            return 16;
        default:
            return 1;
    }
}

bool areTypesCompatible(MaterialValueType from, MaterialValueType to) {
    // Same type is always compatible
    if (from == to) return true;
    
    // Float types can be converted
    if ((from == MaterialValueType::Float || from == MaterialValueType::Float2 ||
         from == MaterialValueType::Float3 || from == MaterialValueType::Float4) &&
        (to == MaterialValueType::Float || to == MaterialValueType::Float2 ||
         to == MaterialValueType::Float3 || to == MaterialValueType::Float4)) {
        return true;
    }
    
    // Int can convert to float
    if (from == MaterialValueType::Int && to == MaterialValueType::Float) {
        return true;
    }
    
    // Bool can convert to float
    if (from == MaterialValueType::Bool && to == MaterialValueType::Float) {
        return true;
    }
    
    return false;
}

// MaterialNode implementation

MaterialPin* MaterialNode::getInput(uint32_t index) {
    return (index < inputs_.size()) ? &inputs_[index] : nullptr;
}

MaterialPin* MaterialNode::getOutput(uint32_t index) {
    return (index < outputs_.size()) ? &outputs_[index] : nullptr;
}

MaterialPin* MaterialNode::findInput(const std::string& name) {
    for (auto& pin : inputs_) {
        if (pin.name == name) return &pin;
    }
    return nullptr;
}

MaterialPin* MaterialNode::findOutput(const std::string& name) {
    for (auto& pin : outputs_) {
        if (pin.name == name) return &pin;
    }
    return nullptr;
}

std::string MaterialNode::getOutputVar(uint32_t pinIndex) const {
    // Generate unique variable name based on node ID and output index
    std::stringstream ss;
    ss << "node" << id << "_out" << pinIndex;
    return ss.str();
}

void MaterialNode::serialize(MaterialSerializer& s) const {
    // Serialize base properties
    float posX = position.x, posY = position.y;
    s.serialize("posX", posX);
    s.serialize("posY", posY);
    
    // Serialize input default values
    s.beginArray("inputDefaults");
    for (const auto& input : inputs_) {
        s.beginObject("");
        
        std::string name = input.name;
        s.serialize("name", name);
        
        // Serialize default value based on type
        std::visit([&s](auto&& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, float>) {
                float v = value;
                s.serialize("floatValue", v);
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                glm::vec2 v = value;
                s.serialize("vec2X", v.x);
                s.serialize("vec2Y", v.y);
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                glm::vec3 v = value;
                s.serialize("vec3X", v.x);
                s.serialize("vec3Y", v.y);
                s.serialize("vec3Z", v.z);
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                glm::vec4 v = value;
                s.serialize("vec4X", v.x);
                s.serialize("vec4Y", v.y);
                s.serialize("vec4Z", v.z);
                s.serialize("vec4W", v.w);
            } else if constexpr (std::is_same_v<T, int32_t>) {
                int32_t v = value;
                s.serialize("intValue", v);
            } else if constexpr (std::is_same_v<T, bool>) {
                bool v = value;
                s.serialize("boolValue", v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                std::string v = value;
                s.serialize("stringValue", v);
            }
        }, input.defaultValue);
        
        s.endObject();
    }
    s.endArray();
}

void MaterialNode::deserialize(MaterialSerializer& s) {
    // Deserialize base properties
    float posX = 0, posY = 0;
    s.serialize("posX", posX);
    s.serialize("posY", posY);
    position = glm::vec2(posX, posY);
    
    // Deserialize input default values
    s.beginArray("inputDefaults");
    size_t count = s.getArraySize();
    for (size_t i = 0; i < count && i < inputs_.size(); i++) {
        s.beginObject("");
        
        std::string name;
        s.serialize("name", name);
        
        // Find matching input
        auto* input = findInput(name);
        if (!input) {
            s.endObject();
            continue;
        }
        
        // Deserialize value based on type
        switch (input->type) {
            case MaterialValueType::Float: {
                float v = 0;
                s.serialize("floatValue", v);
                input->defaultValue = v;
                break;
            }
            case MaterialValueType::Float2: {
                float x = 0, y = 0;
                s.serialize("vec2X", x);
                s.serialize("vec2Y", y);
                input->defaultValue = glm::vec2(x, y);
                break;
            }
            case MaterialValueType::Float3: {
                float x = 0, y = 0, z = 0;
                s.serialize("vec3X", x);
                s.serialize("vec3Y", y);
                s.serialize("vec3Z", z);
                input->defaultValue = glm::vec3(x, y, z);
                break;
            }
            case MaterialValueType::Float4: {
                float x = 0, y = 0, z = 0, w = 0;
                s.serialize("vec4X", x);
                s.serialize("vec4Y", y);
                s.serialize("vec4Z", z);
                s.serialize("vec4W", w);
                input->defaultValue = glm::vec4(x, y, z, w);
                break;
            }
            case MaterialValueType::Int: {
                int32_t v = 0;
                s.serialize("intValue", v);
                input->defaultValue = v;
                break;
            }
            case MaterialValueType::Bool: {
                bool v = false;
                s.serialize("boolValue", v);
                input->defaultValue = v;
                break;
            }
            case MaterialValueType::Texture2D:
            case MaterialValueType::Texture3D:
            case MaterialValueType::TextureCube: {
                std::string v;
                s.serialize("stringValue", v);
                input->defaultValue = v;
                break;
            }
            default:
                break;
        }
        
        s.endObject();
    }
    s.endArray();
}

uint32_t MaterialNode::addInput(const std::string& name, MaterialValueType type) {
    MaterialPin pin(name, type, false);
    pin.id = nextPinId_++;
    inputs_.push_back(std::move(pin));
    return static_cast<uint32_t>(inputs_.size() - 1);
}

uint32_t MaterialNode::addOutput(const std::string& name, MaterialValueType type) {
    MaterialPin pin(name, type, true);
    pin.id = nextPinId_++;
    outputs_.push_back(std::move(pin));
    return static_cast<uint32_t>(outputs_.size() - 1);
}

void MaterialNode::setInputDefault(uint32_t index, float value) {
    if (index < inputs_.size()) {
        inputs_[index].defaultValue = value;
    }
}

void MaterialNode::setInputDefault(uint32_t index, const glm::vec2& value) {
    if (index < inputs_.size()) {
        inputs_[index].defaultValue = value;
    }
}

void MaterialNode::setInputDefault(uint32_t index, const glm::vec3& value) {
    if (index < inputs_.size()) {
        inputs_[index].defaultValue = value;
    }
}

void MaterialNode::setInputDefault(uint32_t index, const glm::vec4& value) {
    if (index < inputs_.size()) {
        inputs_[index].defaultValue = value;
    }
}

void MaterialNode::setInputDefault(uint32_t index, int32_t value) {
    if (index < inputs_.size()) {
        inputs_[index].defaultValue = value;
    }
}

void MaterialNode::setInputDefault(uint32_t index, bool value) {
    if (index < inputs_.size()) {
        inputs_[index].defaultValue = value;
    }
}

void MaterialNode::setInputDefault(uint32_t index, const std::string& value) {
    if (index < inputs_.size()) {
        inputs_[index].defaultValue = value;
    }
}

void MaterialNode::setInputTooltip(uint32_t index, const std::string& tooltip) {
    if (index < inputs_.size()) {
        inputs_[index].tooltip = tooltip;
    }
}

void MaterialNode::setInputHidden(uint32_t index, bool hidden) {
    if (index < inputs_.size()) {
        inputs_[index].hidden = hidden;
    }
}

// MaterialNodeFactory implementation

MaterialNodeFactory& MaterialNodeFactory::getInstance() {
    static MaterialNodeFactory instance;
    return instance;
}

void MaterialNodeFactory::registerNode(const std::string& typeName,
                                        const std::string& category,
                                        CreateFunc creator) {
    creators_[typeName] = NodeTypeInfo{category, std::move(creator)};
}

std::unique_ptr<MaterialNode> MaterialNodeFactory::create(const std::string& typeName) {
    auto it = creators_.find(typeName);
    if (it != creators_.end()) {
        return it->second.creator();
    }
    return nullptr;
}

std::vector<std::string> MaterialNodeFactory::getNodeTypes() const {
    std::vector<std::string> types;
    types.reserve(creators_.size());
    for (const auto& [name, _] : creators_) {
        types.push_back(name);
    }
    std::sort(types.begin(), types.end());
    return types;
}

std::vector<std::string> MaterialNodeFactory::getNodeTypesInCategory(const std::string& category) const {
    std::vector<std::string> types;
    for (const auto& [name, info] : creators_) {
        if (info.category == category) {
            types.push_back(name);
        }
    }
    std::sort(types.begin(), types.end());
    return types;
}

std::vector<std::string> MaterialNodeFactory::getCategories() const {
    std::vector<std::string> categories;
    for (const auto& [name, info] : creators_) {
        if (std::find(categories.begin(), categories.end(), info.category) == categories.end()) {
            categories.push_back(info.category);
        }
    }
    std::sort(categories.begin(), categories.end());
    return categories;
}

bool MaterialNodeFactory::hasType(const std::string& typeName) const {
    return creators_.find(typeName) != creators_.end();
}

// MaterialSerializer implementation (simplified JSON-like format)

void MaterialSerializer::serialize(const std::string& name, float& value) {
    if (writing_) {
        buffer_ += "\"" + name + "\":" + std::to_string(value) + ",";
    } else {
        auto it = currentObject_.find(name);
        if (it != currentObject_.end()) {
            value = std::stof(it->second);
        }
    }
}

void MaterialSerializer::serialize(const std::string& name, glm::vec2& value) {
    serialize(name + "_x", value.x);
    serialize(name + "_y", value.y);
}

void MaterialSerializer::serialize(const std::string& name, glm::vec3& value) {
    serialize(name + "_x", value.x);
    serialize(name + "_y", value.y);
    serialize(name + "_z", value.z);
}

void MaterialSerializer::serialize(const std::string& name, glm::vec4& value) {
    serialize(name + "_x", value.x);
    serialize(name + "_y", value.y);
    serialize(name + "_z", value.z);
    serialize(name + "_w", value.w);
}

void MaterialSerializer::serialize(const std::string& name, int32_t& value) {
    if (writing_) {
        buffer_ += "\"" + name + "\":" + std::to_string(value) + ",";
    } else {
        auto it = currentObject_.find(name);
        if (it != currentObject_.end()) {
            value = std::stoi(it->second);
        }
    }
}

void MaterialSerializer::serialize(const std::string& name, uint32_t& value) {
    int32_t v = static_cast<int32_t>(value);
    serialize(name, v);
    value = static_cast<uint32_t>(v);
}

void MaterialSerializer::serialize(const std::string& name, bool& value) {
    if (writing_) {
        buffer_ += "\"" + name + "\":" + (value ? "true" : "false") + ",";
    } else {
        auto it = currentObject_.find(name);
        if (it != currentObject_.end()) {
            value = (it->second == "true" || it->second == "1");
        }
    }
}

void MaterialSerializer::serialize(const std::string& name, std::string& value) {
    if (writing_) {
        buffer_ += "\"" + name + "\":\"" + value + "\",";
    } else {
        auto it = currentObject_.find(name);
        if (it != currentObject_.end()) {
            value = it->second;
        }
    }
}

void MaterialSerializer::beginObject(const std::string& name) {
    if (writing_) {
        if (!name.empty()) {
            buffer_ += "\"" + name + "\":";
        }
        buffer_ += "{";
    }
}

void MaterialSerializer::endObject() {
    if (writing_) {
        // Remove trailing comma if present
        if (!buffer_.empty() && buffer_.back() == ',') {
            buffer_.pop_back();
        }
        buffer_ += "},";
    }
}

void MaterialSerializer::beginArray(const std::string& name) {
    if (writing_) {
        buffer_ += "\"" + name + "\":[";
    }
}

size_t MaterialSerializer::getArraySize() const {
    return currentArray_.size();
}

void MaterialSerializer::endArray() {
    if (writing_) {
        if (!buffer_.empty() && buffer_.back() == ',') {
            buffer_.pop_back();
        }
        buffer_ += "],";
    }
}

bool MaterialSerializer::saveToFile(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    // Wrap in root object
    std::string output = "{" + buffer_;
    if (!output.empty() && output.back() == ',') {
        output.pop_back();
    }
    output += "}";
    
    file << output;
    return file.good();
}

bool MaterialSerializer::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    std::stringstream ss;
    ss << file.rdbuf();
    
    return fromString(ss.str());
}

std::string MaterialSerializer::toString() const {
    std::string output = "{" + buffer_;
    if (!output.empty() && output.back() == ',') {
        output.pop_back();
    }
    output += "}";
    return output;
}

bool MaterialSerializer::fromString(const std::string& data) {
    writing_ = false;
    buffer_ = data;
    readPos_ = 0;
    // Would need proper JSON parsing here - simplified for now
    return true;
}

nlohmann::json MaterialSerializer::serializeNode(const MaterialNode* node) {
    nlohmann::json json;
    
    if (!node) return json;
    
    json["typeName"] = node->getName();
    json["category"] = node->getCategory();
    json["posX"] = node->position.x;
    json["posY"] = node->position.y;
    
    // Serialize input default values
    nlohmann::json inputsJson = nlohmann::json::array();
    const auto& inputs = node->getInputs();
    for (size_t i = 0; i < inputs.size(); i++) {
        const auto& pin = inputs[i];
        nlohmann::json inputJson;
        inputJson["name"] = pin.name;
        inputJson["type"] = static_cast<int>(pin.type);
        
        // Serialize default value
        std::visit([&inputJson](auto&& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, float>) {
                inputJson["valueType"] = "float";
                inputJson["value"] = value;
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                inputJson["valueType"] = "vec2";
                inputJson["value"] = {value.x, value.y};
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                inputJson["valueType"] = "vec3";
                inputJson["value"] = {value.x, value.y, value.z};
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                inputJson["valueType"] = "vec4";
                inputJson["value"] = {value.x, value.y, value.z, value.w};
            } else if constexpr (std::is_same_v<T, int32_t>) {
                inputJson["valueType"] = "int";
                inputJson["value"] = value;
            } else if constexpr (std::is_same_v<T, bool>) {
                inputJson["valueType"] = "bool";
                inputJson["value"] = value;
            } else if constexpr (std::is_same_v<T, std::string>) {
                inputJson["valueType"] = "string";
                inputJson["value"] = value;
            }
        }, pin.defaultValue);
        
        inputsJson.push_back(inputJson);
    }
    json["inputs"] = inputsJson;
    
    return json;
}

std::unique_ptr<MaterialNode> MaterialSerializer::deserializeNode(const nlohmann::json& json) {
    if (!json.contains("typeName")) return nullptr;
    
    std::string typeName = json["typeName"].get<std::string>();
    
    // Create node using factory
    auto node = MaterialNodeFactory::getInstance().create(typeName);
    if (!node) return nullptr;
    
    // Restore position
    if (json.contains("posX") && json.contains("posY")) {
        node->position.x = json["posX"].get<float>();
        node->position.y = json["posY"].get<float>();
    }
    
    // Restore input default values
    if (json.contains("inputs") && json["inputs"].is_array()) {
        auto& nodeInputs = node->getInputsMutable();
        for (const auto& inputJson : json["inputs"]) {
            if (!inputJson.contains("name")) continue;
            
            std::string name = inputJson["name"].get<std::string>();
            
            // Find matching input by name
            for (auto& input : nodeInputs) {
                if (input.name != name) continue;
                
                if (inputJson.contains("valueType") && inputJson.contains("value")) {
                    std::string valueType = inputJson["valueType"].get<std::string>();
                    
                    if (valueType == "float") {
                        input.defaultValue = inputJson["value"].get<float>();
                    } else if (valueType == "vec2") {
                        auto arr = inputJson["value"];
                        input.defaultValue = glm::vec2(arr[0].get<float>(), arr[1].get<float>());
                    } else if (valueType == "vec3") {
                        auto arr = inputJson["value"];
                        input.defaultValue = glm::vec3(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>());
                    } else if (valueType == "vec4") {
                        auto arr = inputJson["value"];
                        input.defaultValue = glm::vec4(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>(), arr[3].get<float>());
                    } else if (valueType == "int") {
                        input.defaultValue = inputJson["value"].get<int32_t>();
                    } else if (valueType == "bool") {
                        input.defaultValue = inputJson["value"].get<bool>();
                    } else if (valueType == "string") {
                        input.defaultValue = inputJson["value"].get<std::string>();
                    }
                }
                break;
            }
        }
    }
    
    return node;
}

} // namespace Sanic
