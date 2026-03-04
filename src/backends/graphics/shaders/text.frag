#version 450

layout(push_constant) uniform PushConstants {
    mat4 projection;
    vec4 textColor;
} pc;

layout(set = 0, binding = 0) uniform sampler2D glyphTexture;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    float alpha = texture(glyphTexture, fragTexCoord).r;
    outColor = vec4(pc.textColor.rgb, alpha);
}
