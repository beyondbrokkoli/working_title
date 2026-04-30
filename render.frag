#version 460

// Input from Vertex Shader
layout(location = 0) in vec3 v_WorldPos;

// Output to the Swapchain
layout(location = 0) out vec4 f_Color;

void main() {
    // 1. SCREEN-SPACE DERIVATIVES (The Black Magic)
    // The GPU computes pixels in 2x2 blocks. dFdx and dFdy measure the 
    // exact difference in 3D world position between neighboring pixels.
    vec3 dpdx = dFdx(v_WorldPos);
    vec3 dpdy = dFdy(v_WorldPos);
    
    // Cross product of the surface derivatives yields a perfect, un-interpolated face normal.
    vec3 normal = normalize(cross(dpdx, dpdy));
    
    // 2. LIGHTING
    // Hardcoded sun direction (could easily be moved to push constants)
    vec3 sunDir = normalize(vec3(0.5, 1.0, 0.3));
    
    // Lambertian reflectance (dot product) + minimum ambient light
    float dotLight = max(dot(normal, sunDir), 0.0);
    float ambient = 0.15;
    float lightIntensity = dotLight + ambient;
    
    // 3. COLORING
    // VibeEngine Purple base color
    vec3 baseColor = vec3(0.6, 0.2, 0.9);
    
    // Apply lighting and some depth fading (distant particles get darker)
    // Note: gl_FragCoord.z is the depth buffer value (0.0 to 1.0)
    float depthFade = 1.0 - (gl_FragCoord.z * 0.5); 
    
    vec3 finalColor = baseColor * lightIntensity * depthFade;
    
    f_Color = vec4(finalColor, 1.0);
}
