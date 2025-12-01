/**
 * vt_feedback.frag
 * 
 * Virtual Texture feedback shader.
 * Writes page requests to feedback buffer for virtual texture streaming.
 * 
 * Based on UE5's Runtime Virtual Texture system.
 */

#version 450
#extension GL_GOOGLE_include_directive : enable

// Input from vertex shader
layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in float inLodBias;

// Output - feedback buffer (R16G16B16A16_UINT format)
layout(location = 0) out uvec4 outFeedback;

// Virtual texture parameters
layout(set = 0, binding = 0) uniform VTParams {
    vec2 virtualTextureSize;     // Total VT size in texels
    vec2 physicalPageSize;       // Physical page size (e.g., 128x128)
    vec2 tilePadding;            // Border padding for filtering
    float maxMipLevel;           // Maximum mip level
    float mipBias;               // LOD bias
    uint frameIndex;             // For temporal stability
    uint vtIndex;                // Virtual texture index
    vec2 worldOrigin;            // World origin for VT
    vec2 worldSize;              // World size covered by VT
} vt;

// Page table for indirection
layout(set = 0, binding = 1) uniform usampler2D pageTable;

// Calculate mip level based on texture gradients
float calculateMipLevel(vec2 texCoord) {
    vec2 dx = dFdx(texCoord * vt.virtualTextureSize);
    vec2 dy = dFdy(texCoord * vt.virtualTextureSize);
    
    float d = max(dot(dx, dx), dot(dy, dy));
    float mipLevel = 0.5 * log2(d);
    
    return clamp(mipLevel + vt.mipBias + inLodBias, 0.0, vt.maxMipLevel);
}

// Encode page request
uvec4 encodePageRequest(uvec2 pageCoord, uint mipLevel, uint vtIndex) {
    // Format: X, Y, MipLevel, VTIndex
    return uvec4(pageCoord.x, pageCoord.y, mipLevel, vtIndex);
}

void main() {
    // Calculate UV in virtual texture space
    // World position to VT UV
    vec2 vtUV = (inWorldPos.xz - vt.worldOrigin) / vt.worldSize;
    
    // If outside VT bounds, discard
    if (vtUV.x < 0.0 || vtUV.x > 1.0 || vtUV.y < 0.0 || vtUV.y > 1.0) {
        discard;
    }
    
    // Calculate required mip level
    float mipLevelFloat = calculateMipLevel(vtUV);
    uint mipLevel = uint(floor(mipLevelFloat));
    
    // Calculate page coordinate at this mip level
    vec2 mipSize = vt.virtualTextureSize / pow(2.0, float(mipLevel));
    vec2 pageSize = vt.physicalPageSize - vt.tilePadding * 2.0;
    
    uvec2 pageCoord = uvec2(vtUV * mipSize / pageSize);
    
    // Output page request
    outFeedback = encodePageRequest(pageCoord, mipLevel, vt.vtIndex);
}
