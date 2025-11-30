#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragTexCoord;

void main() {
    fragTexCoord = inPosition;
    
    // Remove translation from view matrix
    mat4 view = mat4(mat3(ubo.view));
    vec4 pos = ubo.proj * view * vec4(inPosition, 1.0);
    
    // Set z to w so that z/w = 1.0 (max depth)
    gl_Position = pos.xyww;
}
