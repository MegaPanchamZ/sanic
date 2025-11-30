#version 450

// ============================================================================
// COMPOSITION VERTEX SHADER - Fullscreen Triangle
// Uses vertex ID to generate fullscreen coverage without vertex buffer
// ============================================================================

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Generate fullscreen triangle using vertex ID
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    // This covers the entire screen with a single triangle
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
    
    // Flip Y for Vulkan coordinate system
    fragTexCoord.y = 1.0 - fragTexCoord.y;
}
