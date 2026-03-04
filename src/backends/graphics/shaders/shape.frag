#version 450

layout(push_constant) uniform PushConstants {
    mat4 projection;
    vec2 size;
    float radius;
    float borderWidth;
    int isRounded;
    int isHollow;
    int shapeType;
    int useTexture;
} pc;

layout(location = 0) in vec4 color;
layout(location = 1) in vec2 TexCoord;

layout(location = 0) out vec4 fragment;

// Material Design 3 antialiasing edge width
const float AA_WIDTH = 1.0;

float roundedBoxSDF(vec2 centerPos, vec2 size, float radius) {
    vec2 q = abs(centerPos) - size + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float rectangleSDF(vec2 centerPos, vec2 size) {
    vec2 q = abs(centerPos) - size;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float roundedBorderSDF(vec2 centerPos, vec2 size, float radius, float borderWidth) {
    float outer = pc.isRounded != 0 ?
        roundedBoxSDF(centerPos, size, radius) :
        rectangleSDF(centerPos, size);

    vec2 innerSize = size - vec2(borderWidth);
    if (innerSize.x <= 0.0 || innerSize.y <= 0.0) return outer;

    float innerRadius = max(0.0, radius - borderWidth);
    float inner = pc.isRounded != 0 ?
        roundedBoxSDF(centerPos, innerSize, innerRadius) :
        rectangleSDF(centerPos, innerSize);

    return max(outer, -inner);
}

void main() {
    // Fast path: non-rounded, non-hollow → flat color, skip SDF
    if (pc.isRounded == 0 && pc.isHollow == 0) {
        fragment = color;
        return;
    }

    vec4 baseColor = color;

    if (pc.shapeType == 0) {
        vec2 centerPos = (TexCoord - 0.5) * pc.size;
        vec2 halfSize = pc.size * 0.5;

        if (pc.isHollow != 0) {
            float distance = roundedBorderSDF(centerPos, halfSize, pc.radius, pc.borderWidth);
            float alpha = 1.0 - smoothstep(-AA_WIDTH, AA_WIDTH, distance);
            baseColor.a *= alpha;
            if (baseColor.a < 0.01) discard;
        } else {
            float distance = pc.isRounded != 0 ?
                roundedBoxSDF(centerPos, halfSize, pc.radius) :
                rectangleSDF(centerPos, halfSize);
            float alpha = 1.0 - smoothstep(-AA_WIDTH, AA_WIDTH, distance);
            baseColor.a *= alpha;
            if (baseColor.a < 0.01) discard;
        }
    }

    fragment = baseColor;
}
