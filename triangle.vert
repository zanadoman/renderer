#version 450

layout(set = 1, binding = 0) uniform UBO { mat4 projection; };
layout(location = 0) out vec3 fragColor;

void main() {
    vec4 positions[3] = vec4[](
        vec4( 0.0,  0.5, -1.0, 1.0),
        vec4(-0.5, -0.5, -1.0, 1.0),
        vec4( 0.5, -0.5, -1.0, 1.0)
    );
    vec3 colors[3] = vec3[](
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, 1.0, 0.0),
        vec3(0.0, 0.0, 1.0)
    );
    gl_Position = positions[gl_VertexIndex] * projection;
    fragColor = colors[gl_VertexIndex];
}
