/**
 * TextureCompression.cpp
 * 
 * Implementation of KTX2/Basis texture compression and transcoding.
 * Includes Oodle-style block decompression patterns.
 */

#include "TextureCompression.h"
#include "VulkanContext.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <cmath>
#include <chrono>

// KTX2 magic bytes
static constexpr uint8_t KTX2_IDENTIFIER[12] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};

// Supercompression schemes
enum KTX2Supercompression : uint32_t {
    KTX2_SS_NONE = 0,
    KTX2_SS_BASIS_LZ = 1,       // Basis ETC1S + LZ
    KTX2_SS_ZSTD = 2,           // Zstandard
    KTX2_SS_ZLIB = 3,           // Zlib
};

namespace Sanic {

TextureCompression::TextureCompression() = default;

TextureCompression::~TextureCompression() {
    shutdown();
}

bool TextureCompression::initialize(VulkanContext* context, const CompressionConfig& config) {
    if (initialized_) return true;
    
    context_ = context;
    config_ = config;
    
    // Query GPU format support
    supportedFormats_[CompressedFormat::BC1_RGB] = true;
    supportedFormats_[CompressedFormat::BC1_RGBA] = true;
    supportedFormats_[CompressedFormat::BC3_RGBA] = true;
    supportedFormats_[CompressedFormat::BC4_R] = true;
    supportedFormats_[CompressedFormat::BC5_RG] = true;
    supportedFormats_[CompressedFormat::BC7_RGBA] = true;
    supportedFormats_[CompressedFormat::RGBA8] = true;
    supportedFormats_[CompressedFormat::RGBA16F] = true;
    
    // ASTC support check
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(context_->getPhysicalDevice(), &features);
    
    if (features.textureCompressionASTC_LDR) {
        supportedFormats_[CompressedFormat::ASTC_4x4_RGBA] = true;
        supportedFormats_[CompressedFormat::ASTC_6x6_RGBA] = true;
        supportedFormats_[CompressedFormat::ASTC_8x8_RGBA] = true;
    }
    
    if (features.textureCompressionETC2) {
        supportedFormats_[CompressedFormat::ETC1_RGB] = true;
        supportedFormats_[CompressedFormat::ETC2_RGB] = true;
        supportedFormats_[CompressedFormat::ETC2_RGBA] = true;
    }
    
    // Initialize transcoders
    initBasisTranscoder();
    initOodleDecompressor();
    
    initialized_ = true;
    return true;
}

void TextureCompression::shutdown() {
    if (!initialized_) return;
    
    // Free all textures
    for (auto& [id, tex] : textures_) {
        if (tex.mappedFile) {
            // Unmap file if memory-mapped
            // Platform-specific unmapping would go here
        }
    }
    textures_.clear();
    
    // Cleanup transcoder
    if (basisTranscoder_.context) {
        // Free Basis context
        basisTranscoder_.context = nullptr;
    }
    
    // Cleanup decompressor
    if (oodleDecompressor_.scratchBuffer) {
        delete[] static_cast<uint8_t*>(oodleDecompressor_.scratchBuffer);
        oodleDecompressor_.scratchBuffer = nullptr;
    }
    
    initialized_ = false;
}

bool TextureCompression::initBasisTranscoder() {
    // In production, you'd initialize basisu_transcoder here
    // For now, we simulate the interface
    basisTranscoder_.initialized = true;
    return true;
}

bool TextureCompression::initOodleDecompressor() {
    // Allocate scratch buffer for decompression
    // Oodle typically needs ~256KB-1MB scratch space
    oodleDecompressor_.scratchSize = 1024 * 1024;  // 1MB
    oodleDecompressor_.scratchBuffer = new uint8_t[oodleDecompressor_.scratchSize];
    return true;
}

bool TextureCompression::parseKTX2Header(const uint8_t* data, size_t size, KTX2Header& header) {
    if (size < sizeof(KTX2Header)) {
        return false;
    }
    
    // Check magic bytes
    if (memcmp(data, KTX2_IDENTIFIER, 12) != 0) {
        return false;
    }
    
    // Parse header
    memcpy(&header, data, sizeof(KTX2Header));
    
    // Validate
    if (header.pixelWidth == 0 || header.pixelHeight == 0) {
        return false;
    }
    
    if (header.levelCount == 0) {
        header.levelCount = 1;
    }
    
    return true;
}

bool TextureCompression::parseLevelIndex(const uint8_t* data, const KTX2Header& header,
                                          std::vector<KTX2LevelIndex>& indices) {
    // Level index immediately follows header
    const size_t indexOffset = sizeof(KTX2Header);
    const size_t indexSize = header.levelCount * sizeof(KTX2LevelIndex);
    
    indices.resize(header.levelCount);
    memcpy(indices.data(), data + indexOffset, indexSize);
    
    return true;
}

CompressedFormat TextureCompression::determineSourceFormat(const KTX2Header& header) {
    // Check supercompression scheme
    switch (header.supercompressionScheme) {
        case KTX2_SS_BASIS_LZ:
            return CompressedFormat::ETC1S;
        case KTX2_SS_NONE:
        case KTX2_SS_ZSTD:
        case KTX2_SS_ZLIB:
            // Check vkFormat for UASTC
            // VK_FORMAT_UNDEFINED with BasisLZ metadata means UASTC
            if (header.vkFormat == 0) {
                return CompressedFormat::UASTC;
            }
            break;
    }
    
    // Map Vulkan format to our enum
    switch (header.vkFormat) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            return CompressedFormat::BC1_RGB;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            return CompressedFormat::BC1_RGBA;
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return CompressedFormat::BC3_RGBA;
        case VK_FORMAT_BC4_UNORM_BLOCK:
            return CompressedFormat::BC4_R;
        case VK_FORMAT_BC5_UNORM_BLOCK:
            return CompressedFormat::BC5_RG;
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return CompressedFormat::BC7_RGBA;
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
            return CompressedFormat::ASTC_4x4_RGBA;
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
            return CompressedFormat::ETC2_RGB;
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
            return CompressedFormat::ETC2_RGBA;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return CompressedFormat::RGBA8;
        default:
            return CompressedFormat::Unknown;
    }
}

VkFormat TextureCompression::toVulkanFormat(CompressedFormat format) const {
    switch (format) {
        case CompressedFormat::BC1_RGB:
            return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case CompressedFormat::BC1_RGBA:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case CompressedFormat::BC3_RGBA:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case CompressedFormat::BC4_R:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case CompressedFormat::BC5_RG:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case CompressedFormat::BC7_RGBA:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        case CompressedFormat::ASTC_4x4_RGBA:
            return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case CompressedFormat::ASTC_6x6_RGBA:
            return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        case CompressedFormat::ASTC_8x8_RGBA:
            return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
        case CompressedFormat::ETC1_RGB:
        case CompressedFormat::ETC2_RGB:
            return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
        case CompressedFormat::ETC2_RGBA:
            return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
        case CompressedFormat::RGBA8:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case CompressedFormat::RGBA16F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

uint32_t TextureCompression::loadKTX2(const std::string& path) {
    // Read file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return 0;
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> fileData(fileSize);
    if (!file.read(reinterpret_cast<char*>(fileData.data()), fileSize)) {
        return 0;
    }
    
    return loadFromMemory(fileData.data(), fileSize, path);
}

uint32_t TextureCompression::loadFromMemory(const void* data, size_t size, const std::string& name) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    
    // Parse header
    KTX2Header header;
    if (!parseKTX2Header(bytes, size, header)) {
        return 0;
    }
    
    // Parse level index
    std::vector<KTX2LevelIndex> levelIndices;
    if (!parseLevelIndex(bytes, header, levelIndices)) {
        return 0;
    }
    
    // Create texture entry
    uint32_t textureId = nextTextureId_++;
    CompressedTexture& texture = textures_[textureId];
    
    texture.path = name;
    texture.width = header.pixelWidth;
    texture.height = header.pixelHeight;
    texture.mipLevels = header.levelCount;
    texture.arrayLayers = std::max(1u, header.layerCount);
    texture.sourceFormat = determineSourceFormat(header);
    texture.isTranscoded = false;
    texture.mipTranscoded.resize(header.levelCount, false);
    texture.mips.resize(header.levelCount);
    
    // Load mip data
    for (uint32_t level = 0; level < header.levelCount; ++level) {
        const KTX2LevelIndex& idx = levelIndices[level];
        
        CompressedMipData& mip = texture.mips[level];
        mip.width = std::max(1u, header.pixelWidth >> level);
        mip.height = std::max(1u, header.pixelHeight >> level);
        mip.format = texture.sourceFormat;
        mip.isSupercompressed = (header.supercompressionScheme != KTX2_SS_NONE);
        
        // Copy compressed data
        mip.data.resize(idx.byteLength);
        memcpy(mip.data.data(), bytes + idx.byteOffset, idx.byteLength);
        mip.byteSize = static_cast<uint32_t>(idx.byteLength);
    }
    
    stats_.texturesLoaded++;
    stats_.bytesCompressed += size;
    
    return textureId;
}

CompressedFormat TextureCompression::getOptimalFormat(bool hasAlpha, bool isNormalMap) const {
    // Prefer BC formats on desktop, ASTC on mobile
    if (config_.useASTC && isFormatSupported(CompressedFormat::ASTC_4x4_RGBA)) {
        return CompressedFormat::ASTC_4x4_RGBA;
    }
    
    if (isNormalMap) {
        return CompressedFormat::BC5_RG;
    }
    
    if (hasAlpha) {
        return isFormatSupported(CompressedFormat::BC7_RGBA) 
            ? CompressedFormat::BC7_RGBA 
            : CompressedFormat::BC3_RGBA;
    }
    
    return CompressedFormat::BC1_RGB;
}

bool TextureCompression::isFormatSupported(CompressedFormat format) const {
    auto it = supportedFormats_.find(format);
    return it != supportedFormats_.end() && it->second;
}

bool TextureCompression::transcode(uint32_t textureId, CompressedFormat targetFormat) {
    auto it = textures_.find(textureId);
    if (it == textures_.end()) {
        return false;
    }
    
    CompressedTexture& texture = it->second;
    
    // Determine target format
    if (targetFormat == CompressedFormat::Unknown) {
        bool hasAlpha = (texture.sourceFormat == CompressedFormat::UASTC);
        targetFormat = getOptimalFormat(hasAlpha, false);
    }
    
    if (!isFormatSupported(targetFormat)) {
        targetFormat = CompressedFormat::RGBA8;  // Fallback
    }
    
    texture.transcodedFormat = targetFormat;
    
    // Transcode all mips
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (uint32_t level = 0; level < texture.mipLevels; ++level) {
        if (!transcodeMip(textureId, level)) {
            return false;
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    texture.isTranscoded = true;
    stats_.texturesTranscoded++;
    stats_.totalTranscodeTimeMs += duration.count();
    
    return true;
}

bool TextureCompression::transcodeMip(uint32_t textureId, uint32_t mipLevel) {
    auto it = textures_.find(textureId);
    if (it == textures_.end()) {
        return false;
    }
    
    CompressedTexture& texture = it->second;
    if (mipLevel >= texture.mipLevels) {
        return false;
    }
    
    CompressedMipData& mip = texture.mips[mipLevel];
    
    // Check if already transcoded
    if (texture.mipTranscoded[mipLevel]) {
        return true;
    }
    
    std::vector<uint8_t> decompressed;
    const uint8_t* inputData = mip.data.data();
    size_t inputSize = mip.data.size();
    
    // Step 1: Oodle-style decompression if supercompressed
    if (mip.isSupercompressed) {
        // Estimate output size (typically 2-4x compressed size for Oodle/Zstd)
        size_t estimatedSize = inputSize * 4;
        if (!decompressBlock(inputData, inputSize, decompressed, estimatedSize)) {
            return false;
        }
        inputData = decompressed.data();
        inputSize = decompressed.size();
    }
    
    // Step 2: Basis transcoding
    std::vector<uint8_t> transcoded;
    bool success = false;
    
    if (texture.sourceFormat == CompressedFormat::UASTC) {
        success = transcodeBasisUASTC(inputData, inputSize, transcoded,
                                       mip.width, mip.height, texture.transcodedFormat);
    } else if (texture.sourceFormat == CompressedFormat::ETC1S) {
        success = transcodeBasisETC1S(inputData, inputSize, transcoded,
                                       mip.width, mip.height, texture.transcodedFormat);
    } else {
        // Already in target format, just copy
        transcoded.assign(inputData, inputData + inputSize);
        success = true;
    }
    
    if (!success) {
        return false;
    }
    
    // Replace mip data with transcoded
    mip.data = std::move(transcoded);
    mip.byteSize = static_cast<uint32_t>(mip.data.size());
    mip.format = texture.transcodedFormat;
    mip.isSupercompressed = false;
    texture.mipTranscoded[mipLevel] = true;
    
    stats_.bytesTranscoded += mip.byteSize;
    
    return true;
}

bool TextureCompression::transcodeBasisUASTC(const uint8_t* input, size_t inputSize,
                                               std::vector<uint8_t>& output,
                                               uint32_t width, uint32_t height,
                                               CompressedFormat targetFormat) {
    // In production, use basisu_transcoder here
    // This is a simplified fallback that decodes to RGBA8 then recompresses
    
    // UASTC block size is 16 bytes per 4x4 block
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    
    // For now, decode to RGBA8 (simplified - real impl would use Basis API)
    std::vector<uint8_t> rgba(width * height * 4);
    
    // Simulate UASTC decode (placeholder)
    // Real implementation would call: basist::basisu_transcoder::transcode_image_level()
    for (size_t i = 0; i < rgba.size(); ++i) {
        rgba[i] = 128;  // Gray placeholder
    }
    
    // Now compress to target format
    uint32_t outputSize = getCompressedSize(width, height, targetFormat);
    output.resize(outputSize);
    
    if (targetFormat == CompressedFormat::BC1_RGB || targetFormat == CompressedFormat::BC1_RGBA) {
        // BC1 compression
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                uint8_t blockRGBA[64];  // 4x4 * 4 channels
                
                // Extract 4x4 block
                for (int py = 0; py < 4; ++py) {
                    for (int px = 0; px < 4; ++px) {
                        int x = std::min(bx * 4 + px, width - 1);
                        int y = std::min(by * 4 + py, height - 1);
                        
                        for (int c = 0; c < 4; ++c) {
                            blockRGBA[(py * 4 + px) * 4 + c] = rgba[(y * width + x) * 4 + c];
                        }
                    }
                }
                
                compressBlockBC1(blockRGBA, output.data() + (by * blocksX + bx) * 8);
            }
        }
    } else if (targetFormat == CompressedFormat::BC3_RGBA) {
        // BC3 compression (DXT5)
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                uint8_t blockRGBA[64];
                
                for (int py = 0; py < 4; ++py) {
                    for (int px = 0; px < 4; ++px) {
                        int x = std::min(bx * 4 + px, width - 1);
                        int y = std::min(by * 4 + py, height - 1);
                        
                        for (int c = 0; c < 4; ++c) {
                            blockRGBA[(py * 4 + px) * 4 + c] = rgba[(y * width + x) * 4 + c];
                        }
                    }
                }
                
                compressBlockBC3(blockRGBA, output.data() + (by * blocksX + bx) * 16);
            }
        }
    } else if (targetFormat == CompressedFormat::BC7_RGBA) {
        // BC7 compression
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                uint8_t blockRGBA[64];
                
                for (int py = 0; py < 4; ++py) {
                    for (int px = 0; px < 4; ++px) {
                        int x = std::min(bx * 4 + px, width - 1);
                        int y = std::min(by * 4 + py, height - 1);
                        
                        for (int c = 0; c < 4; ++c) {
                            blockRGBA[(py * 4 + px) * 4 + c] = rgba[(y * width + x) * 4 + c];
                        }
                    }
                }
                
                compressBlockBC7(blockRGBA, output.data() + (by * blocksX + bx) * 16);
            }
        }
    } else if (targetFormat == CompressedFormat::RGBA8) {
        output = std::move(rgba);
    } else {
        return false;
    }
    
    return true;
}

bool TextureCompression::transcodeBasisETC1S(const uint8_t* input, size_t inputSize,
                                               std::vector<uint8_t>& output,
                                               uint32_t width, uint32_t height,
                                               CompressedFormat targetFormat) {
    // Similar to UASTC but for ETC1S format
    // ETC1S is lower quality but smaller
    
    // Simplified: decode to RGBA then recompress
    return transcodeBasisUASTC(input, inputSize, output, width, height, targetFormat);
}

bool TextureCompression::decompressBlock(const uint8_t* input, size_t inputSize,
                                           std::vector<uint8_t>& output, size_t outputSize) {
    // Oodle/Kraken-style decompression
    // This implements a simplified LZ77 variant
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    output.resize(outputSize);
    size_t outputPos = 0;
    size_t inputPos = 0;
    
    while (inputPos < inputSize && outputPos < outputSize) {
        uint8_t token = input[inputPos++];
        
        // High 4 bits = literal count, low 4 bits = match length
        uint32_t literalCount = (token >> 4) & 0xF;
        uint32_t matchLength = (token & 0xF) + 4;  // Minimum match is 4
        
        // Extended literal count
        if (literalCount == 15) {
            uint8_t extra;
            do {
                if (inputPos >= inputSize) break;
                extra = input[inputPos++];
                literalCount += extra;
            } while (extra == 255);
        }
        
        // Copy literals
        if (inputPos + literalCount > inputSize) break;
        if (outputPos + literalCount > outputSize) break;
        
        memcpy(output.data() + outputPos, input + inputPos, literalCount);
        outputPos += literalCount;
        inputPos += literalCount;
        
        // End of block check
        if (inputPos >= inputSize) break;
        
        // Read offset (2 bytes, little endian)
        if (inputPos + 2 > inputSize) break;
        uint16_t offset = input[inputPos] | (input[inputPos + 1] << 8);
        inputPos += 2;
        
        // Extended match length
        if ((token & 0xF) == 15) {
            uint8_t extra;
            do {
                if (inputPos >= inputSize) break;
                extra = input[inputPos++];
                matchLength += extra;
            } while (extra == 255);
        }
        
        // Copy match (with overlap handling)
        if (offset == 0 || outputPos < offset) break;
        if (outputPos + matchLength > outputSize) matchLength = outputSize - outputPos;
        
        size_t matchPos = outputPos - offset;
        for (size_t i = 0; i < matchLength; ++i) {
            output[outputPos++] = output[matchPos++];
        }
    }
    
    output.resize(outputPos);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
    
    oodleDecompressor_.bytesDecompressed += outputPos;
    oodleDecompressor_.decompressTimeNs += duration.count();
    stats_.totalDecompressTimeMs += duration.count() / 1000000;
    
    return outputPos > 0;
}

// BC1 compression (DXT1)
void TextureCompression::compressBlockBC1(const uint8_t* rgba, uint8_t* output) {
    // Find min/max colors
    uint8_t minR = 255, minG = 255, minB = 255;
    uint8_t maxR = 0, maxG = 0, maxB = 0;
    
    for (int i = 0; i < 16; ++i) {
        minR = std::min(minR, rgba[i * 4 + 0]);
        minG = std::min(minG, rgba[i * 4 + 1]);
        minB = std::min(minB, rgba[i * 4 + 2]);
        maxR = std::max(maxR, rgba[i * 4 + 0]);
        maxG = std::max(maxG, rgba[i * 4 + 1]);
        maxB = std::max(maxB, rgba[i * 4 + 2]);
    }
    
    // Convert to RGB565
    uint16_t color0 = ((maxR >> 3) << 11) | ((maxG >> 2) << 5) | (maxB >> 3);
    uint16_t color1 = ((minR >> 3) << 11) | ((minG >> 2) << 5) | (minB >> 3);
    
    // Ensure color0 > color1 for 4-color mode
    if (color0 < color1) {
        std::swap(color0, color1);
        std::swap(minR, maxR);
        std::swap(minG, maxG);
        std::swap(minB, maxB);
    }
    
    // Write colors
    output[0] = color0 & 0xFF;
    output[1] = color0 >> 8;
    output[2] = color1 & 0xFF;
    output[3] = color1 >> 8;
    
    // Generate indices
    uint32_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int r = rgba[i * 4 + 0];
        int g = rgba[i * 4 + 1];
        int b = rgba[i * 4 + 2];
        
        // Simple nearest color selection
        int d0 = (r - maxR) * (r - maxR) + (g - maxG) * (g - maxG) + (b - maxB) * (b - maxB);
        int d1 = (r - minR) * (r - minR) + (g - minG) * (g - minG) + (b - minB) * (b - minB);
        
        // Interpolated colors
        int midR = (2 * maxR + minR) / 3;
        int midG = (2 * maxG + minG) / 3;
        int midB = (2 * maxB + minB) / 3;
        int d2 = (r - midR) * (r - midR) + (g - midG) * (g - midG) + (b - midB) * (b - midB);
        
        midR = (maxR + 2 * minR) / 3;
        midG = (maxG + 2 * minG) / 3;
        midB = (maxB + 2 * minB) / 3;
        int d3 = (r - midR) * (r - midR) + (g - midG) * (g - midG) + (b - midB) * (b - midB);
        
        int minD = std::min({d0, d1, d2, d3});
        uint32_t idx = (minD == d0) ? 0 : (minD == d2) ? 2 : (minD == d3) ? 3 : 1;
        
        indices |= (idx << (i * 2));
    }
    
    // Write indices
    output[4] = indices & 0xFF;
    output[5] = (indices >> 8) & 0xFF;
    output[6] = (indices >> 16) & 0xFF;
    output[7] = (indices >> 24) & 0xFF;
}

// BC3 compression (DXT5)
void TextureCompression::compressBlockBC3(const uint8_t* rgba, uint8_t* output) {
    // Alpha block (8 bytes)
    uint8_t minA = 255, maxA = 0;
    for (int i = 0; i < 16; ++i) {
        minA = std::min(minA, rgba[i * 4 + 3]);
        maxA = std::max(maxA, rgba[i * 4 + 3]);
    }
    
    output[0] = maxA;
    output[1] = minA;
    
    // Alpha indices (48 bits = 16 * 3 bits)
    uint64_t alphaIndices = 0;
    for (int i = 0; i < 16; ++i) {
        uint8_t a = rgba[i * 4 + 3];
        
        // Find nearest of 8 alpha levels
        uint32_t idx = 0;
        int minDist = 256;
        
        for (int j = 0; j < 8; ++j) {
            int level;
            if (j == 0) level = maxA;
            else if (j == 1) level = minA;
            else level = (maxA * (8 - j) + minA * (j - 1)) / 7;
            
            int dist = std::abs(a - level);
            if (dist < minDist) {
                minDist = dist;
                idx = j;
            }
        }
        
        alphaIndices |= (static_cast<uint64_t>(idx) << (i * 3));
    }
    
    output[2] = alphaIndices & 0xFF;
    output[3] = (alphaIndices >> 8) & 0xFF;
    output[4] = (alphaIndices >> 16) & 0xFF;
    output[5] = (alphaIndices >> 24) & 0xFF;
    output[6] = (alphaIndices >> 32) & 0xFF;
    output[7] = (alphaIndices >> 40) & 0xFF;
    
    // Color block (8 bytes) - same as BC1
    compressBlockBC1(rgba, output + 8);
}

// BC7 compression (simplified - mode 6 only)
void TextureCompression::compressBlockBC7(const uint8_t* rgba, uint8_t* output) {
    // BC7 is complex with multiple modes
    // This is a simplified mode 6 encoder (single subset, 7-bit endpoints + 4-bit alpha)
    
    memset(output, 0, 16);
    
    // Mode 6: 1 subset, 7-bit RGB + 7-bit alpha per endpoint
    output[0] = 0x40;  // Mode 6 bit
    
    // Find endpoint colors
    uint8_t minR = 255, minG = 255, minB = 255, minA = 255;
    uint8_t maxR = 0, maxG = 0, maxB = 0, maxA = 0;
    
    for (int i = 0; i < 16; ++i) {
        minR = std::min(minR, rgba[i * 4 + 0]);
        minG = std::min(minG, rgba[i * 4 + 1]);
        minB = std::min(minB, rgba[i * 4 + 2]);
        minA = std::min(minA, rgba[i * 4 + 3]);
        maxR = std::max(maxR, rgba[i * 4 + 0]);
        maxG = std::max(maxG, rgba[i * 4 + 1]);
        maxB = std::max(maxB, rgba[i * 4 + 2]);
        maxA = std::max(maxA, rgba[i * 4 + 3]);
    }
    
    // Pack endpoints (7 bits each)
    // Endpoint 0: R0[6:0], G0[6:0], B0[6:0], A0[6:0]
    // Endpoint 1: R1[6:0], G1[6:0], B1[6:0], A1[6:0]
    // Total: 56 bits
    
    uint64_t endpoints = 0;
    endpoints |= static_cast<uint64_t>(maxR >> 1);           // R0
    endpoints |= static_cast<uint64_t>(maxG >> 1) << 7;      // G0
    endpoints |= static_cast<uint64_t>(maxB >> 1) << 14;     // B0
    endpoints |= static_cast<uint64_t>(maxA >> 1) << 21;     // A0
    endpoints |= static_cast<uint64_t>(minR >> 1) << 28;     // R1
    endpoints |= static_cast<uint64_t>(minG >> 1) << 35;     // G1
    endpoints |= static_cast<uint64_t>(minB >> 1) << 42;     // B1
    endpoints |= static_cast<uint64_t>(minA >> 1) << 49;     // A1
    
    // Write endpoints (starting after mode bit)
    memcpy(output + 1, &endpoints, 7);
    
    // Generate 4-bit indices for 16 pixels (64 bits, but first pixel uses 3 bits)
    uint64_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int r = rgba[i * 4 + 0];
        int g = rgba[i * 4 + 1];
        int b = rgba[i * 4 + 2];
        int a = rgba[i * 4 + 3];
        
        // Find interpolation weight (0-15)
        int totalRange = (maxR - minR) + (maxG - minG) + (maxB - minB) + (maxA - minA);
        int totalDist = (r - minR) + (g - minG) + (b - minB) + (a - minA);
        
        uint32_t idx = (totalRange > 0) ? (totalDist * 15 / totalRange) : 0;
        idx = std::min(15u, idx);
        
        int bits = (i == 0) ? 3 : 4;  // First pixel anchor uses 3 bits
        indices |= (static_cast<uint64_t>(idx >> (4 - bits)) << (i * 4));
    }
    
    memcpy(output + 8, &indices, 8);
}

bool TextureCompression::getTranscodedData(uint32_t textureId, uint32_t mipLevel,
                                            std::vector<uint8_t>& outData) {
    auto it = textures_.find(textureId);
    if (it == textures_.end()) {
        return false;
    }
    
    CompressedTexture& texture = it->second;
    if (mipLevel >= texture.mipLevels) {
        return false;
    }
    
    // Ensure transcoded
    if (!texture.mipTranscoded[mipLevel]) {
        if (!transcodeMip(textureId, mipLevel)) {
            return false;
        }
    }
    
    outData = texture.mips[mipLevel].data;
    return true;
}

VkFormat TextureCompression::getVulkanFormat(uint32_t textureId) const {
    auto it = textures_.find(textureId);
    if (it == textures_.end()) {
        return VK_FORMAT_UNDEFINED;
    }
    
    return toVulkanFormat(it->second.transcodedFormat);
}

bool TextureCompression::getTextureDimensions(uint32_t textureId, 
                                                uint32_t& outWidth, uint32_t& outHeight, 
                                                uint32_t& outMips) const {
    auto it = textures_.find(textureId);
    if (it == textures_.end()) {
        return false;
    }
    
    outWidth = it->second.width;
    outHeight = it->second.height;
    outMips = it->second.mipLevels;
    return true;
}

bool TextureCompression::compressToKTX2(const void* pixels, uint32_t width, uint32_t height,
                                          bool generateMips, std::vector<uint8_t>& outData) {
    const uint8_t* rgba = static_cast<const uint8_t*>(pixels);
    
    // Calculate mip levels
    uint32_t mipLevels = generateMips 
        ? static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1
        : 1;
    
    // Prepare mip data
    std::vector<std::vector<uint8_t>> mipData(mipLevels);
    
    // Generate mips
    std::vector<uint8_t> currentMip(rgba, rgba + width * height * 4);
    uint32_t currentWidth = width;
    uint32_t currentHeight = height;
    
    for (uint32_t level = 0; level < mipLevels; ++level) {
        // Compress to BC7
        uint32_t blocksX = (currentWidth + 3) / 4;
        uint32_t blocksY = (currentHeight + 3) / 4;
        mipData[level].resize(blocksX * blocksY * 16);
        
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                uint8_t blockRGBA[64];
                
                for (int py = 0; py < 4; ++py) {
                    for (int px = 0; px < 4; ++px) {
                        uint32_t x = std::min(bx * 4 + px, currentWidth - 1);
                        uint32_t y = std::min(by * 4 + py, currentHeight - 1);
                        
                        for (int c = 0; c < 4; ++c) {
                            blockRGBA[(py * 4 + px) * 4 + c] = 
                                currentMip[(y * currentWidth + x) * 4 + c];
                        }
                    }
                }
                
                compressBlockBC7(blockRGBA, mipData[level].data() + (by * blocksX + bx) * 16);
            }
        }
        
        // Downsample for next level
        if (level + 1 < mipLevels) {
            uint32_t nextWidth = std::max(1u, currentWidth / 2);
            uint32_t nextHeight = std::max(1u, currentHeight / 2);
            std::vector<uint8_t> nextMip(nextWidth * nextHeight * 4);
            
            for (uint32_t y = 0; y < nextHeight; ++y) {
                for (uint32_t x = 0; x < nextWidth; ++x) {
                    for (int c = 0; c < 4; ++c) {
                        uint32_t sum = 0;
                        sum += currentMip[((y * 2) * currentWidth + (x * 2)) * 4 + c];
                        sum += currentMip[((y * 2) * currentWidth + std::min(x * 2 + 1, currentWidth - 1)) * 4 + c];
                        sum += currentMip[(std::min(y * 2 + 1, currentHeight - 1) * currentWidth + (x * 2)) * 4 + c];
                        sum += currentMip[(std::min(y * 2 + 1, currentHeight - 1) * currentWidth + std::min(x * 2 + 1, currentWidth - 1)) * 4 + c];
                        nextMip[(y * nextWidth + x) * 4 + c] = static_cast<uint8_t>(sum / 4);
                    }
                }
            }
            
            currentMip = std::move(nextMip);
            currentWidth = nextWidth;
            currentHeight = nextHeight;
        }
    }
    
    // Build KTX2 file
    // Calculate sizes
    size_t headerSize = sizeof(KTX2Header);
    size_t indexSize = mipLevels * sizeof(KTX2LevelIndex);
    size_t dataSize = 0;
    for (const auto& mip : mipData) {
        dataSize += mip.size();
    }
    
    outData.resize(headerSize + indexSize + dataSize);
    
    // Write header
    KTX2Header header = {};
    memcpy(header.identifier, KTX2_IDENTIFIER, 12);
    header.vkFormat = VK_FORMAT_BC7_SRGB_BLOCK;
    header.typeSize = 1;
    header.pixelWidth = width;
    header.pixelHeight = height;
    header.pixelDepth = 0;
    header.layerCount = 0;
    header.faceCount = 1;
    header.levelCount = mipLevels;
    header.supercompressionScheme = KTX2_SS_NONE;
    header.dfdByteOffset = 0;
    header.dfdByteLength = 0;
    header.kvdByteOffset = 0;
    header.kvdByteLength = 0;
    header.sgdByteOffset = 0;
    header.sgdByteLength = 0;
    
    memcpy(outData.data(), &header, sizeof(header));
    
    // Write level index
    uint64_t dataOffset = headerSize + indexSize;
    for (uint32_t level = 0; level < mipLevels; ++level) {
        KTX2LevelIndex idx;
        idx.byteOffset = dataOffset;
        idx.byteLength = mipData[level].size();
        idx.uncompressedByteLength = mipData[level].size();
        
        memcpy(outData.data() + headerSize + level * sizeof(KTX2LevelIndex), 
               &idx, sizeof(idx));
        
        // Write mip data
        memcpy(outData.data() + dataOffset, mipData[level].data(), mipData[level].size());
        dataOffset += mipData[level].size();
    }
    
    return true;
}

void TextureCompression::freeTexture(uint32_t textureId) {
    textures_.erase(textureId);
}

TextureCompression::Statistics TextureCompression::getStatistics() const {
    return stats_;
}

} // namespace Sanic

