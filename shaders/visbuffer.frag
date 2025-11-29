#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) flat in uint inMeshletID;
// layout(location = 1) flat in uint inTriangleID; // Use gl_PrimitiveID
layout(location = 2) flat in uint inInstanceID;

layout(location = 0) out uvec2 outVisBuffer;

void main() {
    // 64-bit Visibility Buffer Packing
    // Depth (15 bits) | InstanceID (20 bits) | ClusterID (22 bits) | TriangleID (7 bits)
    
    // Depth: 0.0 to 1.0. Scale to 15 bits (0 to 32767).
    uint64_t depth = uint64_t(gl_FragCoord.z * 32767.0) & 0x7FFF;
    
    uint64_t instanceID = uint64_t(inInstanceID) & 0xFFFFF;
    uint64_t clusterID = uint64_t(inMeshletID) & 0x3FFFFF;
    uint64_t triangleID = uint64_t(gl_PrimitiveID) & 0x7F;
    
    // Pack into 64-bit integer
    // MSB is Depth for atomicMin sorting (if used in compute, but here in frag we rely on HW depth test usually)
    // However, to be consistent with Software Rasterizer which USES atomicMin, we should pack it the same way.
    // AND if we want to use atomicMin in Fragment Shader (to avoid race conditions if we disable HW depth write?),
    // but usually HW depth test is enough for HW rasterizer.
    // BUT, if we want a unified VisBuffer, we should write the same format.
    
    uint64_t packedVal = (depth << 49) | (instanceID << 29) | (clusterID << 7) | triangleID;
    outVisBuffer = unpackUint2x32(packedVal);
}
