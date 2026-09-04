#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(binding = 0) uniform CameraUBO {
    mat4 viewProj;
} camera;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;

void main() {
    gl_Position = camera.viewProj * vec4(inPosition, 1.0);
    outNormal = inNormal;
    outColor  = inColor;
}