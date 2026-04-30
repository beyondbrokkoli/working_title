#version 460

// ==========================================
// 1. YOUR SOA PIPELINE (Direct VRAM Mapping)
// ==========================================
// Binding 0, 1, and 2 match your Descriptor Set setup in main.c
layout(std430, binding = 0) readonly buffer PosX { float lx[]; };
layout(std430, binding = 1) readonly buffer PosY { float ly[]; };
layout(std430, binding = 2) readonly buffer PosZ { float lz[]; };

// ==========================================
// 2. THE PUSH CONSTANT CONNECTION
// ==========================================
// This MUST match a struct in your C code exactly!
// e.g., struct { mat4 viewProj; } PushConstants;
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

// ==========================================
// 3. TETRAHEDRON GENERATOR (12 Vertices)
// ==========================================
const vec3 tet[4] = vec3[](
    vec3( 0.0,  1.0,  0.0), // Top
    vec3(-1.0, -0.5, -0.8), // Bottom Left
    vec3( 1.0, -0.5, -0.8), // Bottom Right
    vec3( 0.0, -0.5,  1.0)  // Front
);

const int indices[12] = int[](
    0, 1, 2,  // Back face
    0, 2, 3,  // Right face
    0, 3, 1,  // Left face
    1, 3, 2   // Bottom face
);

// Output to the Fragment Shader
layout(location = 0) out vec3 v_WorldPos;

void main() {
    // gl_InstanceIndex is the particle ID (0 to draw_count)
    int pId = gl_InstanceIndex;
    
    // Fetch the particle's center point directly from the AVX2 populated arrays
    vec3 center = vec3(lx[pId], ly[pId], lz[pId]);
    
    // gl_VertexIndex is the vertex of the current tetrahedron (0 to 11)
    int localIdx = indices[gl_VertexIndex % 12];
    vec3 localPos = tet[localIdx];
    
    // Scale the tetrahedron and place it at the particle's center
    float particleScale = 2.0; // Adjust this to make your swarm bigger/smaller
    vec3 worldPos = center + (localPos * particleScale);
    
    v_WorldPos = worldPos;
    
    // Project to the screen
    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
}
