/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef ESP32
#include "helpers_gles3.h"
#include "core/aroma_logger.h"
#include "aroma_abi.h"

bool check_shader_compile(GLuint shader)
{
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        LOG_ERROR("ERROR: Shader compilation failed\n%s\n", infoLog);
        return false;
    }

    return true;
}
bool check_shader_link(GLuint program)
{
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        LOG_ERROR("ERROR: Program linking failed\n%s\n", infoLog);
        return false;
}
    return true;
}

static void get_window_size(size_t window_id, int *window_width, int *window_height)
{
    AromaPlatformInterface* platform_interface = aroma_backend_abi.get_platform_interface();
    platform_interface->get_window_size(window_id, window_width, window_height);
}
void convert_coords_to_ndc(size_t window_id, float *ndc_x, float *ndc_y, int x, int y)
{
    int window_width, window_height;
    get_window_size(window_id, &window_width, &window_height);
    *ndc_x = (2.0f * x / window_width) - 1.0f;
    *ndc_y = 1.0f - (2.0f * y / window_height);
}

void convert_dimension_to_ndc(size_t window_id, float *ndc_w, float *ndc_h, int width, int height)
{
    int window_width, window_height;
    get_window_size(window_id, &window_width, &window_height);

    *ndc_w = (2.0f * width) / window_width;
    *ndc_h = -(2.0f * height) / window_height;
}

void convert_hex_to_rgb(vec3 *rgb, unsigned int color_hex)
{
    (*rgb)[0] = ((color_hex >> 16) & 0xFF) / 255.0f;
    (*rgb)[1] = ((color_hex >> 8) & 0xFF) / 255.0f;
    (*rgb)[2] = ((color_hex) & 0xFF) / 255.0f;
}
#endif
