import re

with open('src/core/aroma_3d.c', 'r') as f:
    content = f.read()

# 1. Add STB image include
content = content.replace('#include "cJSON.h"', '#include "cJSON.h"\n#include "../backends/graphics/utils/stb_image.h"')

# 2. Add GLTFImage and GLTFTexture types
types = """
typedef struct {
    int buffer_view;
    char *mime_type;
} GLTFImage;

typedef struct {
    int source_image;
} GLTFTexture;
"""
content = re.sub(r'typedef struct \{\n    float base_color\[4\];\n\} GLTFMaterial;', types + '\ntypedef struct {\n    float base_color[4];\n    int base_color_texture;\n} GLTFMaterial;', content)

# 3. Add to GLTFDocument
doc_fields = """
    GLTFImage *images;
    int image_count;
    GLTFTexture *textures;
    int texture_count;
"""
content = content.replace('    GLTFMaterial *materials;\n    int material_count;', '    GLTFMaterial *materials;\n    int material_count;\n' + doc_fields)

# 4. Add to gltf_document_free
free_code = """
    if (doc->images) {
        for (int i = 0; i < doc->image_count; i++) free(doc->images[i].mime_type);
        free(doc->images);
    }
    free(doc->textures);
"""
content = content.replace('    free(doc->materials);\n    free(doc);', free_code + '    free(doc->materials);\n    free(doc);')

# 5. Add texture parsing
texture_parse = """
    cJSON *images_val = cJSON_GetObjectItemCaseSensitive(root, "images");
    if (images_val && cJSON_IsArray(images_val)) {
        doc->image_count = (int)cJSON_GetArraySize(images_val);
        doc->images = (GLTFImage *)calloc(doc->image_count, sizeof(GLTFImage));
        for (int i = 0; i < doc->image_count; i++) {
            cJSON *img_val = cJSON_GetArrayItem(images_val, i);
            if (img_val && cJSON_IsObject(img_val)) {
                cJSON *bv = cJSON_GetObjectItemCaseSensitive(img_val, "bufferView");
                if (bv) doc->images[i].buffer_view = (int)bv->valuedouble;
                cJSON *mt = cJSON_GetObjectItemCaseSensitive(img_val, "mimeType");
                if (mt && cJSON_IsString(mt)) doc->images[i].mime_type = strdup(mt->valuestring);
            }
        }
    }

    cJSON *textures_val = cJSON_GetObjectItemCaseSensitive(root, "textures");
    if (textures_val && cJSON_IsArray(textures_val)) {
        doc->texture_count = (int)cJSON_GetArraySize(textures_val);
        doc->textures = (GLTFTexture *)calloc(doc->texture_count, sizeof(GLTFTexture));
        for (int i = 0; i < doc->texture_count; i++) {
            cJSON *tex_val = cJSON_GetArrayItem(textures_val, i);
            if (tex_val && cJSON_IsObject(tex_val)) {
                cJSON *src = cJSON_GetObjectItemCaseSensitive(tex_val, "source");
                if (src) doc->textures[i].source_image = (int)src->valuedouble;
            }
        }
    }
"""
content = content.replace('    cJSON *materials_val = cJSON_GetObjectItemCaseSensitive(root, "materials");', texture_parse + '    cJSON *materials_val = cJSON_GetObjectItemCaseSensitive(root, "materials");')

# 6. Parse baseColorTexture in material
mat_parse = """
                    cJSON *bct = cJSON_GetObjectItemCaseSensitive(pbr, "baseColorTexture");
                    if (bct && cJSON_IsObject(bct)) {
                        cJSON *idx = cJSON_GetObjectItemCaseSensitive(bct, "index");
                        if (idx) mat.base_color_texture = (int)idx->valuedouble;
                        else mat.base_color_texture = -1;
                    } else { mat.base_color_texture = -1; }
"""
content = content.replace('                } else { mat.base_color[0] = 0.8f; mat.base_color[1] = 0.8f; mat.base_color[2] = 0.8f; mat.base_color[3] = 1.0f; }', mat_parse + '                } else { mat.base_color[0] = 0.8f; mat.base_color[1] = 0.8f; mat.base_color[2] = 0.8f; mat.base_color[3] = 1.0f; mat.base_color_texture = -1; }')
content = content.replace('GLTFMaterial mat; memset(&mat, 0, sizeof(mat));', 'GLTFMaterial mat; memset(&mat, 0, sizeof(mat)); mat.base_color_texture = -1;')

# 7. Upload texture in build_mesh_from_primitive
upload_code = """
    if (prim->material >= 0 && prim->material < doc->material_count) {
        GLTFMaterial *mat = &doc->materials[prim->material];
        if (mat->base_color_texture >= 0 && mat->base_color_texture < doc->texture_count) {
            GLTFTexture *tex = &doc->textures[mat->base_color_texture];
            if (tex->source_image >= 0 && tex->source_image < doc->image_count) {
                GLTFImage *img = &doc->images[tex->source_image];
                if (img->buffer_view >= 0 && img->buffer_view < doc->view_count) {
                    GLTFBufferView *v = &doc->views[img->buffer_view];
                    const unsigned char *img_data = gltf_buffer_data(doc, v->buffer, v->byte_offset);
                    if (img_data) {
                        int w, h, channels;
                        unsigned char *pixels = stbi_load_from_memory(img_data, v->byte_length, &w, &h, &channels, 4);
                        if (pixels) {
                            glGenTextures(1, &mesh->texture_id);
                            glBindTexture(GL_TEXTURE_2D, mesh->texture_id);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                            mesh->has_texture = true;
                            stbi_image_free(pixels);
                        }
                    }
                }
            }
        }
    }
"""
content = content.replace('    glBindVertexArray(0);\n    return true;\n}', '    glBindVertexArray(0);\n' + upload_code + '    return true;\n}')

with open('src/core/aroma_3d.c', 'w') as f:
    f.write(content)
