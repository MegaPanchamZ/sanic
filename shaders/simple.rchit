#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

// Ray payloads
layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT float shadowPayload;  // 0.0 = shadowed, 1.0 = lit
hitAttributeEXT vec2 attribs;

// TLAS for secondary rays
layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

// Vertex structure matching CPU side
struct Vertex {
    vec3 pos;
    vec3 color;
    vec2 texCoord;
    vec3 normal;
};

// Buffer references for bindless access
layout(buffer_reference, scalar) buffer VertexBuffer { Vertex vertices[]; };
layout(buffer_reference, scalar) buffer IndexBuffer { uint indices[]; };

// Geometry info for each object instance
struct GeometryInfo {
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint textureIndex;
    uint padding;
};

// Descriptor set bindings
layout(set = 0, binding = 3, scalar) readonly buffer GeometryInfoBuffer {
    GeometryInfo geometries[];
} geometryInfos;

layout(set = 0, binding = 4) uniform sampler2D textures[16];  // Array of textures

// UBO for lighting
layout(set = 0, binding = 2) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
} ubo;

void main() {
    // Get geometry info for this instance
    GeometryInfo geomInfo = geometryInfos.geometries[gl_InstanceCustomIndexEXT];
    
    // Get buffer references
    VertexBuffer vertexBuffer = VertexBuffer(geomInfo.vertexBufferAddress);
    IndexBuffer indexBuffer = IndexBuffer(geomInfo.indexBufferAddress);
    
    // Get triangle indices
    uint i0 = indexBuffer.indices[gl_PrimitiveID * 3 + 0];
    uint i1 = indexBuffer.indices[gl_PrimitiveID * 3 + 1];
    uint i2 = indexBuffer.indices[gl_PrimitiveID * 3 + 2];
    
    // Get vertices
    Vertex v0 = vertexBuffer.vertices[i0];
    Vertex v1 = vertexBuffer.vertices[i1];
    Vertex v2 = vertexBuffer.vertices[i2];
    
    // Compute barycentric coordinates
    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    
    // Interpolate vertex attributes in object space
    vec3 localPos = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    vec3 localNormal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
    vec2 texCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;
    
    // Transform to world space
    vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(localPos, 1.0));
    vec3 normal = normalize(mat3(gl_ObjectToWorldEXT) * localNormal);
    
    // Sample texture
    vec3 albedo;
    if (geomInfo.textureIndex < 16) {
        albedo = texture(textures[nonuniformEXT(geomInfo.textureIndex)], texCoord).rgb;
    } else {
        albedo = v0.color * barycentrics.x + v1.color * barycentrics.y + v2.color * barycentrics.z;
    }
    
    // Light direction (directional light)
    vec3 lightDir = normalize(ubo.lightPos.xyz);
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // ========================================================================
    // SHADOW RAY - trace towards light to check occlusion
    // ========================================================================
    float shadow = 1.0;
    if (NdotL > 0.0) {
        shadowPayload = 0.0;  // Assume shadowed - miss shader sets to 1.0
        
        // Offset origin slightly along normal to avoid self-intersection
        vec3 shadowOrigin = worldPos + normal * 0.001;
        
        // Trace shadow ray
        // Using SkipClosestHitShader + TerminateOnFirstHit for efficiency
        // If we hit anything, payload stays 0.0 (shadowed)
        // If we miss (reach the light), miss shader sets payload to 1.0
        traceRayEXT(topLevelAS,
                    gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                    0xFF,           // cullMask
                    0,              // sbtRecordOffset (not used with SkipClosestHitShader)
                    0,              // sbtRecordStride
                    1,              // missIndex (shadow miss = index 1 in miss region)
                    shadowOrigin,   // origin
                    0.001,          // tMin
                    lightDir,       // direction
                    100.0,          // tMax
                    1               // payload location
        );
        
        shadow = shadowPayload;
    }
    
    // ========================================================================
    // LIGHTING CALCULATION
    // ========================================================================
    // Soft shadow: blend between full shadow and lit based on shadow value
    // Add minimum shadow value for softer look (simulates ambient occlusion/bounce light)
    float softShadow = mix(0.3, 1.0, shadow);  // Never fully black in shadow
    
    vec3 ambient = 0.15 * albedo;  // Slightly higher ambient for softer look
    vec3 diffuse = softShadow * NdotL * albedo * ubo.lightColor.rgb;
    
    // Specular (Blinn-Phong)
    vec3 viewDir = normalize(ubo.viewPos.xyz - worldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 32.0);
    vec3 specular = softShadow * spec * ubo.lightColor.rgb * 0.5;
    
    // Add some sky ambient for areas facing up
    vec3 skyAmbient = max(0.0, normal.y) * 0.05 * vec3(0.6, 0.7, 0.9);
    
    hitValue = ambient + diffuse + specular + skyAmbient;
}
