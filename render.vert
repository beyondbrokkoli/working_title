#version 450

// Standard Vertex Input (Matches location = 0 in C)
layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

void main() {
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
}
