#version 450

layout(set = 1, binding = 0) uniform UBO { mat4 mvp; };

layout(location = 0) in  vec3 position;
layout(location = 1) in  vec3 color;
layout(location = 0) out vec3 out_color;

void main() {
    gl_Position = vec4(position, 1.0) * mvp;
    out_color   = color;
}
