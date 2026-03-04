#version 450

layout(push_constant) uniform PushConstants {
    mat4 projection;
} pc;

layout(location = 0) in vec2 pos;
layout(location = 1) in vec4 col;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = pc.projection * vec4(pos, 0.0, 1.0);
    fragColor = col;
    fragTexCoord = texCoord;
}
