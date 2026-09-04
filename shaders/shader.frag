#version 450

// Linked to framebuffer at location 0.
layout(location = 0) out vec4 outColor;
layout(location = 0) in vec3 fragColor;


void main() {

    outColor = vec4(fragColor, 1.0);

}

