/**
 * TextureCompression.h
 * 
 * GPU texture compression using KTX2/Basis Universal and Oodle-style decompression.
 * 
 * Key features:
 * - KTX2 file format support with Basis Universal supercompression
 * - Runtime transcoding to optimal GPU format (BC1-BC7, ASTC, ETC2)
 * - Oodle-style block decompression (Kraken/Mermaid patterns)
 * - Progressive mip loading with compression
 * - UASTC and ETC1S mode support
 * 
 * Compression Pipeline:
 * 1. Load KTX2 file with Basis supercompression
 * 2. Determine optimal target format for GPU
 * 3. Transcode on-the-fly during streaming
 * 4. Upload compressed blocks directly to GPU
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace Sanic {

// Forward declarations
class VulkanContext;

/**
 * Supported compressed formats
 */
enum class CompressedFormat : uint8_t {
    // BC formats (DirectX)
    BC1_RGB,            // 4 bpp, RGB
    BC1_RGBA,           // 4 bpp, RGB + 1-bit alpha
    BC3_RGBA,           // 8 bpp, RGBA
    BC4_R,              // 4 bpp, grayscale
    BC5_RG,             // 8 bpp, normal maps
    BC7_RGBA,           // 8 bpp, high-quality RGBA
    
    // ASTC formats (mobile)
    ASTC_4x4_RGBA,      // 8 bpp
    ASTC_6x6_RGBA,      // 3.56 bpp
    ASTC_8x8_RGBA,      // 2 bpp
    
    // ETC formats (OpenGL ES)
    ETC1_RGB,           // 4 bpp
    ETC2_RGB,           // 4 bpp
    ETC2_RGBA,          // 8 bpp
    
    // Universal formats
    UASTC,              // Universal ASTC (pre-transcoding)
    ETC1S,              // Basis ETC1S (pre-transcoding)
    
    // Uncompressed
    RGBA8,
    RGBA16F,
    
    Unknown
};

/**
 * KTX2 file header (simplified)
 */
struct KTX2Header {
    uint8_t identifier[12];
    uint32_t vkFormat;
    uint32_t typeSize;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
    uint32_t pixelDepth;
    uint32_t layerCount;
    uint32_t faceCount;
    uint32_t levelCount;
    uint32_t supercompressionScheme;
    
    // Data format descriptor
    uint32_t dfdByteOffset;
    uint32_t dfdByteLength;
    
    // Key/Value data
    uint32_t kvdByteOffset;
    uint32_t kvdByteLength;
    
    // Supercompression global data
    uint64_t sgdByteOffset;
    uint64_t sgdByteLength;
};

/**
 * KTX2 level index entry
 */
struct KTX2LevelIndex {
    uint64_t byteOffset;
    uint64_t byteLength;
    uint64_t uncompressedByteLength;
};

/**
 * Basis Universal transcoder state
 */
struct BasisTranscoder {
    void* context = nullptr;
    bool initialized = false;
    
    // Transcoding statistics
    uint64_t bytesTranscoded = 0;
    uint64_t transcodeTimeNs = 0;
};

/**
 * Oodle-style block decompression
 * Implements Kraken/Mermaid-style LZ decompression
 */
struct OodleDecompressor {
    // Decompression context
    void* scratchBuffer = nullptr;
    size_t scratchSize = 0;
    
    // Stats
    uint64_t bytesDecompressed = 0;
    uint64_t decompressTimeNs = 0;
};

/**
 * Compressed mip level data
 */
struct CompressedMipData {
    std::vector<uint8_t> data;
    uint32_t width;
    uint32_t height;
    uint32_t byteSize;
    CompressedFormat format;
    bool isSupercompressed;     // Needs Oodle-style decompression first
};

/**
 * Compressed texture asset
 */
struct CompressedTexture {
    std::string path;
    
    // KTX2 metadata
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    uint32_t arrayLayers;
    CompressedFormat sourceFormat;      // Basis UASTC or ETC1S
    CompressedFormat transcodedFormat;  // GPU native format
    
    // Mip data
    std::vector<CompressedMipData> mips;
    
    // File mapping for streaming
    void* mappedFile = nullptr;
    size_t mappedSize = 0;
    
    // Transcoding state
    bool isTranscoded = false;
    std::vector<bool> mipTranscoded;
};

/**
 * Texture compression configuration
 */
struct CompressionConfig {
    // Target format selection
    CompressedFormat preferredFormat = CompressedFormat::BC7_RGBA;
    bool useASTC = false;               // Prefer ASTC on mobile
    bool useETC2 = false;               // Fallback for older mobile
    
    // Quality settings
    uint32_t encodeQuality = 128;       // 1-255, higher = slower/better
    bool useUASTC = true;               // Use UASTC for quality, ETC1S for size
    
    // Oodle-style supercompression
    bool useSupercompression = true;
    uint32_t compressionLevel = 6;      // 1-10, like Kraken levels
    
    // Transcoding
    uint32_t transcoderThreads = 4;
    bool asyncTranscode = true;
};

/**
 * Texture compression and transcoding system
 */
class TextureCompression {
public:
    TextureCompression();
    ~TextureCompression();
    
    /**
     * Initialize compression system
     */
    bool initialize(VulkanContext* context, const CompressionConfig& config = {});
    
    /**
     * Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * Load a KTX2 texture file
     * @param path Path to KTX2 file
     * @return Texture ID or 0 on failure
     */
    uint32_t loadKTX2(const std::string& path);
    
    /**
     * Load a compressed texture from memory
     */
    uint32_t loadFromMemory(const void* data, size_t size, const std::string& name = "");
    
    /**
     * Transcode a texture to GPU format
     * @param textureId Texture to transcode
     * @param targetFormat Target GPU format (or Unknown for auto-select)
     * @return True if transcoding succeeded
     */
    bool transcode(uint32_t textureId, CompressedFormat targetFormat = CompressedFormat::Unknown);
    
    /**
     * Transcode a single mip level (for streaming)
     */
    bool transcodeMip(uint32_t textureId, uint32_t mipLevel);
    
    /**
     * Get transcoded data for upload
     * @param textureId Texture ID
     * @param mipLevel Mip level to get
     * @param outData Output buffer (will be resized)
     * @return True if data is available
     */
    bool getTranscodedData(uint32_t textureId, uint32_t mipLevel, std::vector<uint8_t>& outData);
    
    /**
     * Get Vulkan format for transcoded texture
     */
    VkFormat getVulkanFormat(uint32_t textureId) const;
    
    /**
     * Get texture dimensions
     */
    bool getTextureDimensions(uint32_t textureId, uint32_t& outWidth, uint32_t& outHeight, uint32_t& outMips) const;
    
    /**
     * Compress an uncompressed texture to KTX2
     * @param pixels RGBA8 pixel data
     * @param width Image width
     * @param height Image height
     * @param generateMips Generate mip levels
     * @param outData Output KTX2 data
     */
    bool compressToKTX2(const void* pixels, uint32_t width, uint32_t height,
                        bool generateMips, std::vector<uint8_t>& outData);
    
    /**
     * Free a texture
     */
    void freeTexture(uint32_t textureId);
    
    /**
     * Get optimal format for current GPU
     */
    CompressedFormat getOptimalFormat(bool hasAlpha, bool isNormalMap) const;
    
    /**
     * Check if format is supported
     */
    bool isFormatSupported(CompressedFormat format) const;
    
    // Statistics
    struct Statistics {
        uint64_t texturesLoaded;
        uint64_t texturesTranscoded;
        uint64_t bytesCompressed;
        uint64_t bytesTranscoded;
        uint64_t totalTranscodeTimeMs;
        uint64_t totalDecompressTimeMs;
    };
    Statistics getStatistics() const;
    
private:
    // Internal methods
    bool parseKTX2Header(const uint8_t* data, size_t size, KTX2Header& header);
    bool parseLevelIndex(const uint8_t* data, const KTX2Header& header, 
                         std::vector<KTX2LevelIndex>& indices);
    
    CompressedFormat determineSourceFormat(const KTX2Header& header);
    VkFormat toVulkanFormat(CompressedFormat format) const;
    
    // Basis transcoding
    bool initBasisTranscoder();
    bool transcodeBasisUASTC(const uint8_t* input, size_t inputSize,
                              std::vector<uint8_t>& output,
                              uint32_t width, uint32_t height,
                              CompressedFormat targetFormat);
    bool transcodeBasisETC1S(const uint8_t* input, size_t inputSize,
                              std::vector<uint8_t>& output,
                              uint32_t width, uint32_t height,
                              CompressedFormat targetFormat);
    
    // Oodle-style decompression
    bool initOodleDecompressor();
    bool decompressBlock(const uint8_t* input, size_t inputSize,
                          std::vector<uint8_t>& output, size_t outputSize);
    
    // BC compression helpers
    void compressBlockBC1(const uint8_t* rgba, uint8_t* output);
    void compressBlockBC3(const uint8_t* rgba, uint8_t* output);
    void compressBlockBC7(const uint8_t* rgba, uint8_t* output);
    
    VulkanContext* context_ = nullptr;
    CompressionConfig config_;
    
    // Texture storage
    std::unordered_map<uint32_t, CompressedTexture> textures_;
    uint32_t nextTextureId_ = 1;
    
    // Transcoding
    BasisTranscoder basisTranscoder_;
    OodleDecompressor oodleDecompressor_;
    
    // GPU format support cache
    std::unordered_map<CompressedFormat, bool> supportedFormats_;
    
    // Statistics
    Statistics stats_ = {};
    
    bool initialized_ = false;
};

/**
 * Helper: Calculate block-compressed image size
 */
inline uint32_t getCompressedSize(uint32_t width, uint32_t height, CompressedFormat format) {
    uint32_t blockWidth = (width + 3) / 4;
    uint32_t blockHeight = (height + 3) / 4;
    
    switch (format) {
        case CompressedFormat::BC1_RGB:
        case CompressedFormat::BC1_RGBA:
        case CompressedFormat::BC4_R:
        case CompressedFormat::ETC1_RGB:
        case CompressedFormat::ETC2_RGB:
            return blockWidth * blockHeight * 8;  // 8 bytes per block
            
        case CompressedFormat::BC3_RGBA:
        case CompressedFormat::BC5_RG:
        case CompressedFormat::BC7_RGBA:
        case CompressedFormat::ETC2_RGBA:
        case CompressedFormat::ASTC_4x4_RGBA:
            return blockWidth * blockHeight * 16; // 16 bytes per block
            
        case CompressedFormat::ASTC_6x6_RGBA: {
            uint32_t bw = (width + 5) / 6;
            uint32_t bh = (height + 5) / 6;
            return bw * bh * 16;
        }
            
        case CompressedFormat::ASTC_8x8_RGBA: {
            uint32_t bw = (width + 7) / 8;
            uint32_t bh = (height + 7) / 8;
            return bw * bh * 16;
        }
            
        case CompressedFormat::RGBA8:
            return width * height * 4;
            
        case CompressedFormat::RGBA16F:
            return width * height * 8;
            
        default:
            return 0;
    }
}

} // namespace Sanic

