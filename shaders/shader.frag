#version 450

// Linked to framebuffer at location 0.
layout(location = 0) out vec4 outColor;
layout(location = 0) in vec3 fragColor;


void main() {


    // Unlike the vert shader, the fragment shader does not have built in variablet o pass the color for the current fragment
    // Instead, the buffer at location 0 is used. 
    outColor = vec4(fragColor, 1.0);

}

