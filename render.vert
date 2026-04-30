#version 450

// Standard Vertex Input from our C AoS buffer
layout(location = 0) in vec3 inPosition;

// NEW: The Output to satisfy the Fragment Shader
layout(location = 0) out vec3 fragColor; 

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

void main() {
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
    
    // Pass a hot pink debug color to the fragment shader (to match your image!)
    fragColor = vec3(1.0, 0.5, 1.0); 
}
