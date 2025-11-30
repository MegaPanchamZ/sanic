/**
 * SceneSerializerAdvanced.cpp
 * 
 * Implementation of enhanced scene serialization with reflection.
 */

#include "SceneSerializerAdvanced.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace Sanic {

// ============================================================================
// REFLECTION COMPONENT SERIALIZER
// ============================================================================

ReflectionComponentSerializer::ReflectionComponentSerializer(TypeDescriptor* typeDesc)
    : typeDesc_(typeDesc) {
}

void ReflectionComponentSerializer::serialize(const void* component, std::ostream& stream, 
                                               SceneFormat format) const {
    if (format == SceneFormat::JSON) {
        stream << "{";
        bool first = true;
        for (const auto& prop : typeDesc_->properties) {
            if (!(prop.flags & PropertyFlags::Serialize)) continue;
            
            if (!first) stream << ",";
            first = false;
            
            stream << "\"" << prop.name << "\":";
            serializePropertyJSON(prop, component, stream);
        }
        stream << "}";
    } else {
        // Binary format
        for (const auto& prop : typeDesc_->properties) {
            if (!(prop.flags & PropertyFlags::Serialize)) continue;
            serializePropertyBinary(prop, component, stream);
        }
    }
}

void ReflectionComponentSerializer::deserialize(void* component, std::istream& stream,
                                                  SceneFormat format) const {
    if (format == SceneFormat::JSON) {
        JSONReader reader(stream);
        reader.expectToken(JSONReader::Token::ObjectStart);
        
        while (reader.peekToken() != JSONReader::Token::ObjectEnd) {
            std::string key = reader.readString();
            reader.expectToken(JSONReader::Token::Colon);
            
            // Find property
            const PropertyMeta* prop = nullptr;
            for (const auto& p : typeDesc_->properties) {
                if (p.name == key) {
                    prop = &p;
                    break;
                }
            }
            
            if (prop) {
                deserializePropertyJSON(*prop, component, stream);
            } else {
                reader.skipValue();
            }
            
            if (reader.peekToken() == JSONReader::Token::Comma) {
                reader.nextToken();
            }
        }
        reader.expectToken(JSONReader::Token::ObjectEnd);
    } else {
        for (const auto& prop : typeDesc_->properties) {
            if (!(prop.flags & PropertyFlags::Serialize)) continue;
            deserializePropertyBinary(prop, component, stream);
        }
    }
}

void ReflectionComponentSerializer::addToEntity(World& world, Entity entity,
                                                  std::istream& stream, SceneFormat format) const {
    // TODO: Use reflection to add component dynamically
    // This requires runtime component creation support in ECS
}

void ReflectionComponentSerializer::serializePropertyJSON(const PropertyMeta& prop,
                                                           const void* data,
                                                           std::ostream& stream) const {
    const uint8_t* ptr = static_cast<const uint8_t*>(data) + prop.offset;
    
    switch (prop.type) {
        case PropertyType::Bool:
            stream << (*reinterpret_cast<const bool*>(ptr) ? "true" : "false");
            break;
        case PropertyType::Int32:
            stream << *reinterpret_cast<const int32_t*>(ptr);
            break;
        case PropertyType::Int64:
            stream << *reinterpret_cast<const int64_t*>(ptr);
            break;
        case PropertyType::UInt32:
            stream << *reinterpret_cast<const uint32_t*>(ptr);
            break;
        case PropertyType::UInt64:
            stream << *reinterpret_cast<const uint64_t*>(ptr);
            break;
        case PropertyType::Float:
            stream << std::fixed << std::setprecision(6) << *reinterpret_cast<const float*>(ptr);
            break;
        case PropertyType::Double:
            stream << std::fixed << std::setprecision(12) << *reinterpret_cast<const double*>(ptr);
            break;
        case PropertyType::String: {
            const std::string& str = *reinterpret_cast<const std::string*>(ptr);
            stream << "\"";
            for (char c : str) {
                switch (c) {
                    case '"': stream << "\\\""; break;
                    case '\\': stream << "\\\\"; break;
                    case '\n': stream << "\\n"; break;
                    case '\r': stream << "\\r"; break;
                    case '\t': stream << "\\t"; break;
                    default: stream << c; break;
                }
            }
            stream << "\"";
            break;
        }
        case PropertyType::Vec2: {
            const glm::vec2& v = *reinterpret_cast<const glm::vec2*>(ptr);
            stream << "[" << v.x << "," << v.y << "]";
            break;
        }
        case PropertyType::Vec3: {
            const glm::vec3& v = *reinterpret_cast<const glm::vec3*>(ptr);
            stream << "[" << v.x << "," << v.y << "," << v.z << "]";
            break;
        }
        case PropertyType::Vec4: {
            const glm::vec4& v = *reinterpret_cast<const glm::vec4*>(ptr);
            stream << "[" << v.x << "," << v.y << "," << v.z << "," << v.w << "]";
            break;
        }
        case PropertyType::Quat: {
            const glm::quat& q = *reinterpret_cast<const glm::quat*>(ptr);
            stream << "[" << q.x << "," << q.y << "," << q.z << "," << q.w << "]";
            break;
        }
        case PropertyType::Mat4: {
            const glm::mat4& m = *reinterpret_cast<const glm::mat4*>(ptr);
            stream << "[";
            for (int i = 0; i < 16; ++i) {
                if (i > 0) stream << ",";
                stream << m[i / 4][i % 4];
            }
            stream << "]";
            break;
        }
        case PropertyType::Enum:
            stream << *reinterpret_cast<const int32_t*>(ptr);
            break;
        case PropertyType::Entity:
            stream << static_cast<uint32_t>(*reinterpret_cast<const Entity*>(ptr));
            break;
        case PropertyType::Asset:
            // Asset reference
            stream << "\"" << *reinterpret_cast<const std::string*>(ptr) << "\"";
            break;
        default:
            stream << "null";
            break;
    }
}

void ReflectionComponentSerializer::deserializePropertyJSON(const PropertyMeta& prop,
                                                              void* data,
                                                              std::istream& stream) const {
    uint8_t* ptr = static_cast<uint8_t*>(data) + prop.offset;
    JSONReader reader(stream);
    
    switch (prop.type) {
        case PropertyType::Bool:
            *reinterpret_cast<bool*>(ptr) = reader.readBool();
            break;
        case PropertyType::Int32:
            *reinterpret_cast<int32_t*>(ptr) = reader.readInt();
            break;
        case PropertyType::Int64:
            *reinterpret_cast<int64_t*>(ptr) = reader.readInt64();
            break;
        case PropertyType::UInt32:
            *reinterpret_cast<uint32_t*>(ptr) = static_cast<uint32_t>(reader.readInt64());
            break;
        case PropertyType::UInt64:
            *reinterpret_cast<uint64_t*>(ptr) = static_cast<uint64_t>(reader.readInt64());
            break;
        case PropertyType::Float:
            *reinterpret_cast<float*>(ptr) = reader.readFloat();
            break;
        case PropertyType::Double:
            *reinterpret_cast<double*>(ptr) = reader.readDouble();
            break;
        case PropertyType::String:
            *reinterpret_cast<std::string*>(ptr) = reader.readString();
            break;
        case PropertyType::Vec2:
            *reinterpret_cast<glm::vec2*>(ptr) = reader.readVec2();
            break;
        case PropertyType::Vec3:
            *reinterpret_cast<glm::vec3*>(ptr) = reader.readVec3();
            break;
        case PropertyType::Vec4:
            *reinterpret_cast<glm::vec4*>(ptr) = reader.readVec4();
            break;
        case PropertyType::Quat:
            *reinterpret_cast<glm::quat*>(ptr) = reader.readQuat();
            break;
        case PropertyType::Mat4:
            *reinterpret_cast<glm::mat4*>(ptr) = reader.readMat4();
            break;
        case PropertyType::Enum:
            *reinterpret_cast<int32_t*>(ptr) = reader.readInt();
            break;
        case PropertyType::Entity:
            *reinterpret_cast<Entity*>(ptr) = static_cast<Entity>(reader.readInt());
            break;
        case PropertyType::Asset:
            *reinterpret_cast<std::string*>(ptr) = reader.readString();
            break;
        default:
            reader.skipValue();
            break;
    }
}

void ReflectionComponentSerializer::serializePropertyBinary(const PropertyMeta& prop,
                                                              const void* data,
                                                              std::ostream& stream) const {
    const uint8_t* ptr = static_cast<const uint8_t*>(data) + prop.offset;
    
    switch (prop.type) {
        case PropertyType::Bool:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(bool));
            break;
        case PropertyType::Int32:
        case PropertyType::Enum:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(int32_t));
            break;
        case PropertyType::Int64:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(int64_t));
            break;
        case PropertyType::UInt32:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(uint32_t));
            break;
        case PropertyType::UInt64:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(uint64_t));
            break;
        case PropertyType::Float:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(float));
            break;
        case PropertyType::Double:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(double));
            break;
        case PropertyType::String: {
            const std::string& str = *reinterpret_cast<const std::string*>(ptr);
            uint32_t len = static_cast<uint32_t>(str.length());
            stream.write(reinterpret_cast<const char*>(&len), sizeof(len));
            stream.write(str.data(), len);
            break;
        }
        case PropertyType::Vec2:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(glm::vec2));
            break;
        case PropertyType::Vec3:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(glm::vec3));
            break;
        case PropertyType::Vec4:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(glm::vec4));
            break;
        case PropertyType::Quat:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(glm::quat));
            break;
        case PropertyType::Mat4:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(glm::mat4));
            break;
        case PropertyType::Entity:
            stream.write(reinterpret_cast<const char*>(ptr), sizeof(Entity));
            break;
        case PropertyType::Asset: {
            const std::string& path = *reinterpret_cast<const std::string*>(ptr);
            uint64_t guid = AssetReferenceSerializer::get().getOrAssignGUID(path);
            stream.write(reinterpret_cast<const char*>(&guid), sizeof(guid));
            break;
        }
        default:
            break;
    }
}

void ReflectionComponentSerializer::deserializePropertyBinary(const PropertyMeta& prop,
                                                                void* data,
                                                                std::istream& stream) const {
    uint8_t* ptr = static_cast<uint8_t*>(data) + prop.offset;
    
    switch (prop.type) {
        case PropertyType::Bool:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(bool));
            break;
        case PropertyType::Int32:
        case PropertyType::Enum:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(int32_t));
            break;
        case PropertyType::Int64:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(int64_t));
            break;
        case PropertyType::UInt32:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(uint32_t));
            break;
        case PropertyType::UInt64:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(uint64_t));
            break;
        case PropertyType::Float:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(float));
            break;
        case PropertyType::Double:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(double));
            break;
        case PropertyType::String: {
            uint32_t len;
            stream.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string& str = *reinterpret_cast<std::string*>(ptr);
            str.resize(len);
            stream.read(str.data(), len);
            break;
        }
        case PropertyType::Vec2:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(glm::vec2));
            break;
        case PropertyType::Vec3:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(glm::vec3));
            break;
        case PropertyType::Vec4:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(glm::vec4));
            break;
        case PropertyType::Quat:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(glm::quat));
            break;
        case PropertyType::Mat4:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(glm::mat4));
            break;
        case PropertyType::Entity:
            stream.read(reinterpret_cast<char*>(ptr), sizeof(Entity));
            break;
        case PropertyType::Asset: {
            uint64_t guid;
            stream.read(reinterpret_cast<char*>(&guid), sizeof(guid));
            *reinterpret_cast<std::string*>(ptr) = AssetReferenceSerializer::get().resolvePath(guid);
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// JSON WRITER
// ============================================================================

JSONWriter::JSONWriter(std::ostream& stream, bool pretty)
    : stream_(stream), pretty_(pretty) {
}

void JSONWriter::writeIndent() {
    if (pretty_) {
        stream_ << "\n";
        for (int i = 0; i < indent_; ++i) {
            stream_ << "  ";
        }
    }
}

void JSONWriter::comma() {
    if (needsComma_ && !inKey_) {
        stream_ << ",";
        needsComma_ = false;
    }
}

void JSONWriter::beginObject() {
    comma();
    stream_ << "{";
    indent_++;
    needsComma_ = false;
}

void JSONWriter::endObject() {
    indent_--;
    writeIndent();
    stream_ << "}";
    needsComma_ = true;
}

void JSONWriter::beginArray() {
    comma();
    stream_ << "[";
    indent_++;
    needsComma_ = false;
}

void JSONWriter::endArray() {
    indent_--;
    if (pretty_) stream_ << " ";
    stream_ << "]";
    needsComma_ = true;
}

void JSONWriter::key(const std::string& name) {
    comma();
    writeIndent();
    stream_ << "\"" << name << "\":";
    if (pretty_) stream_ << " ";
    inKey_ = true;
    needsComma_ = false;
}

void JSONWriter::value(bool v) {
    comma();
    stream_ << (v ? "true" : "false");
    needsComma_ = true;
    inKey_ = false;
}

void JSONWriter::value(int32_t v) {
    comma();
    stream_ << v;
    needsComma_ = true;
    inKey_ = false;
}

void JSONWriter::value(uint32_t v) {
    comma();
    stream_ << v;
    needsComma_ = true;
    inKey_ = false;
}

void JSONWriter::value(int64_t v) {
    comma();
    stream_ << v;
    needsComma_ = true;
    inKey_ = false;
}

void JSONWriter::value(uint64_t v) {
    comma();
    stream_ << v;
    needsComma_ = true;
    inKey_ = false;
}

void JSONWriter::value(float v) {
    comma();
    if (std::isnan(v)) {
        stream_ << "null";
    } else if (std::isinf(v)) {
        stream_ << (v > 0 ? "1e308" : "-1e308");
    } else {
        stream_ << std::fixed << std::setprecision(6) << v;
    }
    needsComma_ = true;
    inKey_ = false;
}

void JSONWriter::value(double v) {
    comma();
    if (std::isnan(v)) {
        stream_ << "null";
    } else if (std::isinf(v)) {
        stream_ << (v > 0 ? "1e308" : "-1e308");
    } else {
        stream_ << std::fixed << std::setprecision(12) << v;
    }
    needsComma_ = true;
    inKey_ = false;
}

void JSONWriter::escapeString(const std::string& s) {
    stream_ << "\"";
    for (char c : s) {
        switch (c) {
            case '"': stream_ << "\\\""; break;
            case '\\': stream_ << "\\\\"; break;
            case '\b': stream_ << "\\b"; break;
            case '\f': stream_ << "\\f"; break;
            case '\n': stream_ << "\\n"; break;
            case '\r': stream_ << "\\r"; break;
            case '\t': stream_ << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 32) {
                    stream_ << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(c) << std::dec;
                } else {
                    stream_ << c;
                }
                break;
        }
    }
    stream_ << "\"";
}

void JSONWriter::value(const std::string& v) {
    comma();
    escapeString(v);
    needsComma_ = true;
    inKey_ = false;
}

void JSONWriter::value(const char* v) {
    value(std::string(v));
}

void JSONWriter::nullValue() {
    comma();
    stream_ << "null";
    needsComma_ = true;
    inKey_ = false;
}

void JSONWriter::writeVec2(const glm::vec2& v) {
    beginArray();
    value(v.x);
    value(v.y);
    endArray();
}

void JSONWriter::writeVec3(const glm::vec3& v) {
    beginArray();
    value(v.x);
    value(v.y);
    value(v.z);
    endArray();
}

void JSONWriter::writeVec4(const glm::vec4& v) {
    beginArray();
    value(v.x);
    value(v.y);
    value(v.z);
    value(v.w);
    endArray();
}

void JSONWriter::writeQuat(const glm::quat& q) {
    beginArray();
    value(q.x);
    value(q.y);
    value(q.z);
    value(q.w);
    endArray();
}

void JSONWriter::writeMat4(const glm::mat4& m) {
    beginArray();
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            value(m[i][j]);
        }
    }
    endArray();
}

// ============================================================================
// JSON READER
// ============================================================================

JSONReader::JSONReader(std::istream& stream) : stream_(stream) {
}

void JSONReader::skipWhitespace() {
    while (stream_ && std::isspace(stream_.peek())) {
        stream_.get();
    }
}

JSONReader::Token JSONReader::parseToken() {
    skipWhitespace();
    
    if (!stream_) {
        return Token::EndOfFile;
    }
    
    int c = stream_.peek();
    
    switch (c) {
        case '{': stream_.get(); return Token::ObjectStart;
        case '}': stream_.get(); return Token::ObjectEnd;
        case '[': stream_.get(); return Token::ArrayStart;
        case ']': stream_.get(); return Token::ArrayEnd;
        case ':': stream_.get(); return Token::Colon;
        case ',': stream_.get(); return Token::Comma;
        case '"': {
            stream_.get(); // consume opening quote
            currentString_.clear();
            while (stream_) {
                c = stream_.get();
                if (c == '"') break;
                if (c == '\\') {
                    c = stream_.get();
                    switch (c) {
                        case '"': currentString_ += '"'; break;
                        case '\\': currentString_ += '\\'; break;
                        case 'b': currentString_ += '\b'; break;
                        case 'f': currentString_ += '\f'; break;
                        case 'n': currentString_ += '\n'; break;
                        case 'r': currentString_ += '\r'; break;
                        case 't': currentString_ += '\t'; break;
                        case 'u': {
                            char hex[5] = {0};
                            stream_.read(hex, 4);
                            currentString_ += static_cast<char>(std::stoi(hex, nullptr, 16));
                            break;
                        }
                        default: currentString_ += static_cast<char>(c); break;
                    }
                } else {
                    currentString_ += static_cast<char>(c);
                }
            }
            return Token::String;
        }
        case 't':
            stream_.get(); stream_.get(); stream_.get(); stream_.get(); // "true"
            return Token::True;
        case 'f':
            stream_.get(); stream_.get(); stream_.get(); stream_.get(); stream_.get(); // "false"
            return Token::False;
        case 'n':
            stream_.get(); stream_.get(); stream_.get(); stream_.get(); // "null"
            return Token::Null;
        default:
            if (c == '-' || std::isdigit(c)) {
                std::string numStr;
                while (stream_ && (std::isdigit(stream_.peek()) || 
                       stream_.peek() == '.' || stream_.peek() == '-' ||
                       stream_.peek() == '+' || stream_.peek() == 'e' || 
                       stream_.peek() == 'E')) {
                    numStr += static_cast<char>(stream_.get());
                }
                currentNumber_ = std::stod(numStr);
                return Token::Number;
            }
            return Token::Error;
    }
}

JSONReader::Token JSONReader::nextToken() {
    currentToken_ = parseToken();
    return currentToken_;
}

JSONReader::Token JSONReader::peekToken() {
    auto pos = stream_.tellg();
    Token token = parseToken();
    stream_.seekg(pos);
    return token;
}

void JSONReader::expectToken(Token expected) {
    Token actual = nextToken();
    if (actual != expected) {
        throw std::runtime_error("Unexpected JSON token");
    }
}

bool JSONReader::readBool() {
    Token t = nextToken();
    if (t == Token::True) return true;
    if (t == Token::False) return false;
    throw std::runtime_error("Expected boolean");
}

int32_t JSONReader::readInt() {
    expectToken(Token::Number);
    return static_cast<int32_t>(currentNumber_);
}

int64_t JSONReader::readInt64() {
    expectToken(Token::Number);
    return static_cast<int64_t>(currentNumber_);
}

float JSONReader::readFloat() {
    expectToken(Token::Number);
    return static_cast<float>(currentNumber_);
}

double JSONReader::readDouble() {
    expectToken(Token::Number);
    return currentNumber_;
}

std::string JSONReader::readString() {
    expectToken(Token::String);
    return currentString_;
}

glm::vec2 JSONReader::readVec2() {
    expectToken(Token::ArrayStart);
    float x = readFloat();
    expectToken(Token::Comma);
    float y = readFloat();
    expectToken(Token::ArrayEnd);
    return glm::vec2(x, y);
}

glm::vec3 JSONReader::readVec3() {
    expectToken(Token::ArrayStart);
    float x = readFloat();
    expectToken(Token::Comma);
    float y = readFloat();
    expectToken(Token::Comma);
    float z = readFloat();
    expectToken(Token::ArrayEnd);
    return glm::vec3(x, y, z);
}

glm::vec4 JSONReader::readVec4() {
    expectToken(Token::ArrayStart);
    float x = readFloat();
    expectToken(Token::Comma);
    float y = readFloat();
    expectToken(Token::Comma);
    float z = readFloat();
    expectToken(Token::Comma);
    float w = readFloat();
    expectToken(Token::ArrayEnd);
    return glm::vec4(x, y, z, w);
}

glm::quat JSONReader::readQuat() {
    glm::vec4 v = readVec4();
    return glm::quat(v.w, v.x, v.y, v.z);
}

glm::mat4 JSONReader::readMat4() {
    expectToken(Token::ArrayStart);
    glm::mat4 m;
    for (int i = 0; i < 16; ++i) {
        if (i > 0) expectToken(Token::Comma);
        m[i / 4][i % 4] = readFloat();
    }
    expectToken(Token::ArrayEnd);
    return m;
}

bool JSONReader::skipValue() {
    Token t = nextToken();
    switch (t) {
        case Token::ObjectStart: {
            int depth = 1;
            while (depth > 0 && stream_) {
                t = nextToken();
                if (t == Token::ObjectStart) depth++;
                else if (t == Token::ObjectEnd) depth--;
            }
            return true;
        }
        case Token::ArrayStart: {
            int depth = 1;
            while (depth > 0 && stream_) {
                t = nextToken();
                if (t == Token::ArrayStart) depth++;
                else if (t == Token::ArrayEnd) depth--;
            }
            return true;
        }
        case Token::String:
        case Token::Number:
        case Token::True:
        case Token::False:
        case Token::Null:
            return true;
        default:
            return false;
    }
}

// ============================================================================
// ENHANCED SCENE SERIALIZER
// ============================================================================

EnhancedSceneSerializer::EnhancedSceneSerializer() {
    registerReflectedTypes();
}

void EnhancedSceneSerializer::registerReflectedTypes() {
    // Get all registered types from TypeRegistry
    // for (const auto& pair : TypeRegistry::get().getAllTypes()) {
    //     typeDescriptors_[pair.first] = pair.second;
    // }
}

std::string EnhancedSceneSerializer::serializeToJSON(const Scene& scene, bool pretty) {
    std::ostringstream stream;
    JSONWriter writer(stream, pretty);
    
    writer.beginObject();
    
    // Metadata
    writer.key("metadata");
    serializeMetadataJSON(writer, scene.getMetadata());
    
    // Entities
    writer.key("entities");
    writer.beginArray();
    
    for (Entity entity : scene.getRootEntities()) {
        serializeEntityJSON(writer, const_cast<World&>(scene.getWorld()), entity);
    }
    
    writer.endArray();
    
    writer.endObject();
    
    return stream.str();
}

std::unique_ptr<Scene> EnhancedSceneSerializer::deserializeFromJSON(const std::string& json) {
    std::istringstream stream(json);
    JSONReader reader(stream);
    
    auto scene = std::make_unique<Scene>();
    
    reader.expectToken(JSONReader::Token::ObjectStart);
    
    while (reader.peekToken() != JSONReader::Token::ObjectEnd) {
        std::string key = reader.readString();
        reader.expectToken(JSONReader::Token::Colon);
        
        if (key == "metadata") {
            deserializeMetadataJSON(reader, scene->getMetadata());
        } else if (key == "entities") {
            reader.expectToken(JSONReader::Token::ArrayStart);
            
            std::vector<Entity> roots;
            while (reader.peekToken() != JSONReader::Token::ArrayEnd) {
                Entity entity = deserializeEntityJSON(reader, scene->getWorld());
                roots.push_back(entity);
                
                if (reader.peekToken() == JSONReader::Token::Comma) {
                    reader.nextToken();
                }
            }
            scene->setRootEntities(roots);
            
            reader.expectToken(JSONReader::Token::ArrayEnd);
        } else {
            reader.skipValue();
        }
        
        if (reader.peekToken() == JSONReader::Token::Comma) {
            reader.nextToken();
        }
    }
    
    reader.expectToken(JSONReader::Token::ObjectEnd);
    
    return scene;
}

void EnhancedSceneSerializer::serializeMetadataJSON(JSONWriter& writer, 
                                                      const SceneMetadata& metadata) {
    writer.beginObject();
    
    writer.key("name");
    writer.value(metadata.name);
    
    writer.key("description");
    writer.value(metadata.description);
    
    writer.key("author");
    writer.value(metadata.author);
    
    writer.key("ambientColor");
    writer.writeVec3(metadata.ambientColor);
    
    writer.key("skyboxPath");
    writer.value(metadata.skyboxPath);
    
    writer.key("environmentMapPath");
    writer.value(metadata.environmentMapPath);
    
    writer.endObject();
}

void EnhancedSceneSerializer::deserializeMetadataJSON(JSONReader& reader,
                                                        SceneMetadata& metadata) {
    reader.expectToken(JSONReader::Token::ObjectStart);
    
    while (reader.peekToken() != JSONReader::Token::ObjectEnd) {
        std::string key = reader.readString();
        reader.expectToken(JSONReader::Token::Colon);
        
        if (key == "name") {
            metadata.name = reader.readString();
        } else if (key == "description") {
            metadata.description = reader.readString();
        } else if (key == "author") {
            metadata.author = reader.readString();
        } else if (key == "ambientColor") {
            metadata.ambientColor = reader.readVec3();
        } else if (key == "skyboxPath") {
            metadata.skyboxPath = reader.readString();
        } else if (key == "environmentMapPath") {
            metadata.environmentMapPath = reader.readString();
        } else {
            reader.skipValue();
        }
        
        if (reader.peekToken() == JSONReader::Token::Comma) {
            reader.nextToken();
        }
    }
    
    reader.expectToken(JSONReader::Token::ObjectEnd);
}

void EnhancedSceneSerializer::serializeEntityJSON(JSONWriter& writer, World& world, Entity entity) {
    writer.beginObject();
    
    // Entity ID
    writer.key("id");
    writer.value(static_cast<uint32_t>(entity));
    
    // Components
    writer.key("components");
    writer.beginObject();
    
    // Serialize each component using reflection
    // TODO: Iterate through entity's components and serialize each
    
    writer.endObject();
    
    // Children
    writer.key("children");
    writer.beginArray();
    
    for (Entity child : SceneGraph::getChildren(world, entity)) {
        serializeEntityJSON(writer, world, child);
    }
    
    writer.endArray();
    
    writer.endObject();
}

Entity EnhancedSceneSerializer::deserializeEntityJSON(JSONReader& reader, World& world) {
    reader.expectToken(JSONReader::Token::ObjectStart);
    
    Entity entity = world.createEntity();
    
    while (reader.peekToken() != JSONReader::Token::ObjectEnd) {
        std::string key = reader.readString();
        reader.expectToken(JSONReader::Token::Colon);
        
        if (key == "id") {
            // Read but ignore - we create new IDs
            reader.readInt();
        } else if (key == "components") {
            reader.expectToken(JSONReader::Token::ObjectStart);
            
            while (reader.peekToken() != JSONReader::Token::ObjectEnd) {
                deserializeComponentJSON(reader, world, entity);
                
                if (reader.peekToken() == JSONReader::Token::Comma) {
                    reader.nextToken();
                }
            }
            
            reader.expectToken(JSONReader::Token::ObjectEnd);
        } else if (key == "children") {
            reader.expectToken(JSONReader::Token::ArrayStart);
            
            while (reader.peekToken() != JSONReader::Token::ArrayEnd) {
                Entity child = deserializeEntityJSON(reader, world);
                SceneGraph::setParent(world, child, entity);
                
                if (reader.peekToken() == JSONReader::Token::Comma) {
                    reader.nextToken();
                }
            }
            
            reader.expectToken(JSONReader::Token::ArrayEnd);
        } else {
            reader.skipValue();
        }
        
        if (reader.peekToken() == JSONReader::Token::Comma) {
            reader.nextToken();
        }
    }
    
    reader.expectToken(JSONReader::Token::ObjectEnd);
    
    return entity;
}

void EnhancedSceneSerializer::deserializeComponentJSON(JSONReader& reader, World& world, Entity entity) {
    std::string typeName = reader.readString();
    reader.expectToken(JSONReader::Token::Colon);
    
    // Find serializer for this component type
    auto* serializer = ComponentSerializerRegistry::getInstance().getSerializer(typeName);
    if (serializer) {
        // TODO: Create component and deserialize
    } else {
        reader.skipValue();
    }
}

std::vector<uint8_t> EnhancedSceneSerializer::serializeToBinary(const Scene& scene) {
    std::ostringstream stream(std::ios::binary);
    
    // Header
    stream.write(reinterpret_cast<const char*>(&SCENE_MAGIC), sizeof(SCENE_MAGIC));
    stream.write(reinterpret_cast<const char*>(&SCENE_VERSION), sizeof(SCENE_VERSION));
    
    // TODO: Serialize scene in binary format
    
    std::string str = stream.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

std::unique_ptr<Scene> EnhancedSceneSerializer::deserializeFromBinary(const std::vector<uint8_t>& data) {
    std::istringstream stream(std::string(data.begin(), data.end()), std::ios::binary);
    
    // Read and verify header
    uint32_t magic, version;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    if (magic != SCENE_MAGIC) {
        throw std::runtime_error("Invalid scene file magic number");
    }
    
    if (version > SCENE_VERSION) {
        throw std::runtime_error("Scene file version too new");
    }
    
    // TODO: Deserialize scene from binary
    
    return std::make_unique<Scene>();
}

// ============================================================================
// ASSET REFERENCE SERIALIZER
// ============================================================================

AssetReferenceSerializer& AssetReferenceSerializer::get() {
    static AssetReferenceSerializer instance;
    return instance;
}

uint64_t AssetReferenceSerializer::getOrAssignGUID(const std::string& path) {
    auto it = pathToGuid_.find(path);
    if (it != pathToGuid_.end()) {
        return it->second;
    }
    
    uint64_t guid = nextGuid_++;
    pathToGuid_[path] = guid;
    guidToPath_[guid] = path;
    return guid;
}

std::string AssetReferenceSerializer::resolvePath(uint64_t guid) const {
    auto it = guidToPath_.find(guid);
    return it != guidToPath_.end() ? it->second : "";
}

void AssetReferenceSerializer::saveMapping(const std::string& path) {
    std::ofstream file(path);
    file << nextGuid_ << "\n";
    for (const auto& pair : pathToGuid_) {
        file << pair.second << " " << pair.first << "\n";
    }
}

void AssetReferenceSerializer::loadMapping(const std::string& path) {
    std::ifstream file(path);
    if (!file) return;
    
    file >> nextGuid_;
    
    uint64_t guid;
    std::string assetPath;
    while (file >> guid >> std::ws && std::getline(file, assetPath)) {
        pathToGuid_[assetPath] = guid;
        guidToPath_[guid] = assetPath;
    }
}

void AssetReferenceSerializer::updatePath(uint64_t guid, const std::string& newPath) {
    auto it = guidToPath_.find(guid);
    if (it != guidToPath_.end()) {
        pathToGuid_.erase(it->second);
        it->second = newPath;
        pathToGuid_[newPath] = guid;
    }
}

// ============================================================================
// SCENE GRAPH UTILITIES
// ============================================================================

Entity SceneGraph::getParent(World& world, Entity entity) {
    // TODO: Implement parent lookup from hierarchy component
    return INVALID_ENTITY;
}

std::vector<Entity> SceneGraph::getChildren(World& world, Entity entity) {
    // TODO: Implement children lookup from hierarchy component
    return {};
}

std::vector<Entity> SceneGraph::getAllDescendants(World& world, Entity entity) {
    std::vector<Entity> result;
    
    std::function<void(Entity)> collectDescendants = [&](Entity e) {
        for (Entity child : getChildren(world, e)) {
            result.push_back(child);
            collectDescendants(child);
        }
    };
    
    collectDescendants(entity);
    return result;
}

void SceneGraph::setParent(World& world, Entity child, Entity parent) {
    // TODO: Update hierarchy component
}

void SceneGraph::detach(World& world, Entity entity) {
    setParent(world, entity, INVALID_ENTITY);
}

void SceneGraph::destroyHierarchy(World& world, Entity root) {
    // Destroy children first (depth-first)
    for (Entity child : getChildren(world, root)) {
        destroyHierarchy(world, child);
    }
    world.destroyEntity(root);
}

Entity SceneGraph::cloneHierarchy(World& world, Entity root, bool keepRefs) {
    // TODO: Clone entity and all descendants
    return INVALID_ENTITY;
}

void SceneGraph::traverse(World& world, Entity root,
                          std::function<void(Entity, int depth)> visitor) {
    std::function<void(Entity, int)> traverseRecursive = [&](Entity e, int depth) {
        visitor(e, depth);
        for (Entity child : getChildren(world, e)) {
            traverseRecursive(child, depth + 1);
        }
    };
    
    traverseRecursive(root, 0);
}

void SceneGraph::traverseTopDown(World& world, std::function<void(Entity)> visitor) {
    // TODO: Traverse all root entities
}

Entity SceneGraph::findByPath(World& world, Entity root, const std::string& path) {
    // Split path by '/'
    std::vector<std::string> parts;
    std::istringstream stream(path);
    std::string part;
    while (std::getline(stream, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    
    Entity current = root;
    for (const auto& name : parts) {
        bool found = false;
        for (Entity child : getChildren(world, current)) {
            // TODO: Compare with entity name
            // if (getName(world, child) == name) {
            //     current = child;
            //     found = true;
            //     break;
            // }
        }
        if (!found) {
            return INVALID_ENTITY;
        }
    }
    
    return current;
}

std::string SceneGraph::getPath(World& world, Entity entity) {
    std::vector<std::string> parts;
    Entity current = entity;
    
    while (current != INVALID_ENTITY) {
        // TODO: Get entity name
        // parts.push_back(getName(world, current));
        current = getParent(world, current);
    }
    
    std::reverse(parts.begin(), parts.end());
    
    std::string path;
    for (const auto& part : parts) {
        path += "/" + part;
    }
    
    return path;
}

// ============================================================================
// SCENE DIFF
// ============================================================================

std::vector<SceneDiff::Change> SceneDiff::diff(const Scene& from, const Scene& to) {
    std::vector<Change> changes;
    
    // TODO: Compare entities and components between scenes
    
    return changes;
}

void SceneDiff::apply(Scene& scene, const std::vector<Change>& changes) {
    for (const auto& change : changes) {
        switch (change.op) {
            case Operation::CreateEntity:
                // TODO: Create entity with serialized data
                break;
            case Operation::DeleteEntity:
                scene.getWorld().destroyEntity(change.entity);
                break;
            case Operation::AddComponent:
                // TODO: Add component from serialized data
                break;
            case Operation::RemoveComponent:
                // TODO: Remove component by type
                break;
            case Operation::ModifyComponent:
                // TODO: Update component with new data
                break;
            case Operation::ReparentEntity:
                // TODO: Update hierarchy
                break;
            case Operation::RenameEntity:
                // TODO: Update name component
                break;
        }
    }
}

std::vector<SceneDiff::Change> SceneDiff::merge(const std::vector<Change>& local,
                                                  const std::vector<Change>& remote) {
    std::vector<Change> merged;
    
    // TODO: Implement three-way merge with conflict resolution
    
    // Simple strategy: apply remote changes first, then local
    merged.insert(merged.end(), remote.begin(), remote.end());
    merged.insert(merged.end(), local.begin(), local.end());
    
    return merged;
}

std::vector<uint8_t> SceneDiff::serialize(const std::vector<Change>& changes) {
    std::ostringstream stream(std::ios::binary);
    
    uint32_t count = static_cast<uint32_t>(changes.size());
    stream.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    for (const auto& change : changes) {
        uint8_t op = static_cast<uint8_t>(change.op);
        stream.write(reinterpret_cast<const char*>(&op), sizeof(op));
        stream.write(reinterpret_cast<const char*>(&change.entity), sizeof(change.entity));
        
        uint32_t typeLen = static_cast<uint32_t>(change.componentType.length());
        stream.write(reinterpret_cast<const char*>(&typeLen), sizeof(typeLen));
        stream.write(change.componentType.data(), typeLen);
        
        uint32_t dataLen = static_cast<uint32_t>(change.newData.size());
        stream.write(reinterpret_cast<const char*>(&dataLen), sizeof(dataLen));
        stream.write(reinterpret_cast<const char*>(change.newData.data()), dataLen);
    }
    
    std::string str = stream.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

std::vector<SceneDiff::Change> SceneDiff::deserialize(const std::vector<uint8_t>& data) {
    std::istringstream stream(std::string(data.begin(), data.end()), std::ios::binary);
    std::vector<Change> changes;
    
    uint32_t count;
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    for (uint32_t i = 0; i < count; ++i) {
        Change change;
        
        uint8_t op;
        stream.read(reinterpret_cast<char*>(&op), sizeof(op));
        change.op = static_cast<Operation>(op);
        
        stream.read(reinterpret_cast<char*>(&change.entity), sizeof(change.entity));
        
        uint32_t typeLen;
        stream.read(reinterpret_cast<char*>(&typeLen), sizeof(typeLen));
        change.componentType.resize(typeLen);
        stream.read(change.componentType.data(), typeLen);
        
        uint32_t dataLen;
        stream.read(reinterpret_cast<char*>(&dataLen), sizeof(dataLen));
        change.newData.resize(dataLen);
        stream.read(reinterpret_cast<char*>(change.newData.data()), dataLen);
        
        changes.push_back(change);
    }
    
    return changes;
}

} // namespace Sanic
