#version 450
layout(location = 0) in vec3 inPosition;

// The bridge receiving our 16 floats from Lua
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    // Hardware projection!
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
    
    // The pure Cyan glow you asked for
    fragColor = vec3(0.0, 1.0, 1.0); 
}
