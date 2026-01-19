#ifndef HELPERS_H
#define HELPERS_H

#include <GLES3/gl3.h>
#include "linmath.h"
#include <stdbool.h>
#include "platforms/aroma_platform_interface.h"

typedef struct
{
    GLuint textureID;
    int width;
    int height;
    int bearingX;
    int bearingY;
    int advance;
} Character;

typedef struct Vertex
{
    vec2 pos;
    vec3 col;
    vec2 texCoord; 
    float thickness;

} Vertex;

static const char *rectangle_vertex_shader =
    "#version 300 es\n"
    "precision mediump float;\n"
    "layout(location = 0) in vec2 pos;\n"
    "layout(location = 1) in vec3 col;\n"
    "layout(location = 2) in vec2 texCoord;\n"
    "uniform mat4 projection;\n"
    "out vec3 color;\n"
    "out vec2 TexCoord;\n"
    "void main() {\n"
    "    gl_Position = projection * vec4(pos, 0.0, 1.0);\n"
    "    color = col;\n"
    "    TexCoord = texCoord;\n"
    "}\n";

static const char *rectangle_fragment_shader =
"#version 300 es\n"
"precision highp float;\n"
"in vec3 color;\n"
"in vec2 TexCoord;\n"
"out vec4 fragment;\n"
"uniform sampler2D tex;\n"
"uniform bool useTexture;\n"
"uniform vec2 size;\n"
"uniform float radius;\n"
"uniform bool isRounded;\n"
"uniform bool isHollow;\n"
"uniform float borderWidth;\n"
"uniform int shapeType;\n"
"\n"
"// Material Design 3 antialiasing edge width\n"
"const float AA_WIDTH = 1.0;\n"
"\n"
"float roundedBoxSDF(vec2 centerPos, vec2 size, float radius) {\n"
"    vec2 q = abs(centerPos) - size + radius;\n"
"    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;\n"
"}\n"
"\n"
"float rectangleSDF(vec2 centerPos, vec2 size) {\n"
"    vec2 q = abs(centerPos) - size;\n"
"    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);\n"
"}\n"
"\n"
"float roundedBorderSDF(vec2 centerPos, vec2 size, float radius, float borderWidth) {\n"
"    float outer = isRounded ? \n"
"        roundedBoxSDF(centerPos, size, radius) : \n"
"        rectangleSDF(centerPos, size);\n"
"\n"
"    vec2 innerSize = size - vec2(borderWidth);\n"
"    if (innerSize.x <= 0.0 || innerSize.y <= 0.0) return outer;\n"
"\n"
"    float innerRadius = max(0.0, radius - borderWidth);\n"
"    float inner = isRounded ? \n"
"        roundedBoxSDF(centerPos, innerSize, innerRadius) : \n"
"        rectangleSDF(centerPos, innerSize);\n"
"\n"
"    return max(outer, -inner);\n"
"}\n"
"\n"
"void main() {\n"
"    vec4 baseColor = useTexture ? texture(tex, TexCoord) * vec4(color, 1.0) : vec4(color, 1.0);\n"
"\n"
"    if (shapeType == 0) {\n"
"        vec2 centerPos = (TexCoord - 0.5) * size;\n"
"        vec2 halfSize = size * 0.5;\n"
"        \n"
"        if (isHollow) {\n"
"            float distance = roundedBorderSDF(centerPos, halfSize, radius, borderWidth);\n"
"            // Smooth antialiased edges\n"
"            float alpha = 1.0 - smoothstep(-AA_WIDTH, AA_WIDTH, distance);\n"
"            baseColor.a *= alpha;\n"
"            if (baseColor.a < 0.01) discard;\n"
"        } else {\n"
"            float distance = isRounded ? \n"
"                roundedBoxSDF(centerPos, halfSize, radius) : \n"
"                rectangleSDF(centerPos, halfSize);\n"
"            // Smooth antialiased edges\n"
"            float alpha = 1.0 - smoothstep(-AA_WIDTH, AA_WIDTH, distance);\n"
"            baseColor.a *= alpha;\n"
"            if (baseColor.a < 0.01) discard;\n"
"        }\n"
"    }\n"
"\n"
"    fragment = baseColor;\n"
"}\n";
static const char *text_vertex_shader_source =
    "#version 300 es\n"
    "precision mediump float;\n"
    "layout(location = 0) in vec4 vertex;\n"
    "out vec2 TexCoords;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
    "    TexCoords = vec2(vertex.z, 1.0-vertex.w);\n"
    "}\n";
static const char *text_fragment_shader_source =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 TexCoords;\n"
    "out vec4 color;\n"
    "uniform sampler2D text;\n"
    "uniform vec3 textColor;\n"
    "void main() {\n"
    "   float alpha = texture(text, TexCoords).r;\n"
    "   color = vec4(textColor, alpha);\n"
    "}\n";

bool check_shader_link(GLuint program);
bool check_shader_compile(GLuint shader);
void convert_coords_to_ndc(size_t window_id, float *ndc_x, float *ndc_y, int x, int y);
void convert_dimension_to_ndc(size_t window_id, float *ndc_w, float *ndc_h, int width, int height);
void convert_hex_to_rgb(vec3 *rgb, unsigned int color_hex);


#endif