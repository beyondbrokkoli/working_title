#version 450

// The 3 Flat Arrays coming directly from your AVX2 CPU Engine!
layout(std430, binding = 0) readonly buffer BufX { float Vert_PX[]; };
layout(std430, binding = 1) readonly buffer BufY { float Vert_PY[]; };
layout(std430, binding = 2) readonly buffer BufZ { float Vert_PZ[]; };

layout(push_constant) uniform Screen {
    float w;
    float h;
} screen;

layout(location = 0) out vec4 fragColor;

void main() {
    uint id = gl_VertexIndex;
    
    float px = Vert_PX[id];
    float py = Vert_PY[id];
    float pz = Vert_PZ[id]; // The raw, unclipped distance!
    // [THE FIX] Ignore the CPU Graveyard!
    if (pz < 0.1) {
        // Yeet the vertex completely outside the bounds of the screen
        gl_Position = vec4(2.0, 2.0, 2.0, 0.0); 
        return;
    }
    // 1. Convert AVX2 Screen Coordinates (0 to 1920) to Vulkan NDC (-1.0 to 1.0)
    float ndc_x = (px / (screen.w * 0.5)) - 1.0;
    float ndc_y = (py / (screen.h * 0.5)) - 1.0;

    // 2. [THE BUG FIX] Force Z into the safe [0, 1] range so Vulkan draws it!
    gl_Position = vec4(ndc_x, ndc_y, 0.5, 1.0);

    // 3. Dynamic Point Size: Particles get smaller as they get further away
    gl_PointSize = clamp(20000.0 / max(pz, 1.0), 1.0, 6.0);

    // 4. [THE BEAUTY] Use the raw depth to color the Swarm!
    // Normalize the distance (assuming the swarm is roughly 5k to 15k units away)
    float depth = clamp((pz - 5000.0) / 10000.0, 0.0, 1.0);
    
    // Mix from Cyberpunk Pink (close) to Neon Cyan (far)
    vec3 nearColor = vec3(1.0, 0.1, 0.6); 
    vec3 farColor  = vec3(0.0, 0.8, 1.0); 
    vec3 finalColor = mix(nearColor, farColor, depth);

    // Additive blending looks best with a soft alpha
    fragColor = vec4(finalColor, 0.6);
}
