#ifndef ESP32
#include "aroma_3d.h"
#include "backends/graphics/utils/linmath.h"
#include "core/aroma_logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <GLES3/gl3.h>

#include "cJSON.h"
#include "../backends/graphics/utils/stb_image.h"

#define AROMA_3D_MAX_ATTRS 8

#define AROMA_3D_MAX_MESHES 512
#define AROMA_3D_SANITY_MESH_LIMIT 65536

#define AROMA_3D_DEFAULT_METALLIC 0.55f
#define AROMA_3D_DEFAULT_CLEARCOAT 1.0f

typedef struct
{
    int buffer;
    size_t byte_length;
    size_t byte_offset;
    size_t byte_stride;
} GLTFBufferView;

typedef struct
{
    size_t byte_length;
    unsigned char *uri_data;
    size_t uri_size;
    char *uri;
} GLTFBuffer;

typedef struct
{
    int buffer_view;
    int component_type;
    int type;
    size_t count;
    size_t byte_offset;
    float max[3];
    float min[3];
    bool normalized;
} GLTFAccessor;

typedef struct
{
    int mode;
    int indices_accessor;
    int material;
    int pos_accessor;
    int norm_accessor;
    int uv_accessor;
} GLTFPrimitive;

typedef struct
{
    GLTFPrimitive *primitives;
    int primitive_count;
} GLTFMesh;

typedef struct
{
    int buffer_view;
    char *mime_type;
    char *uri;
} GLTFImage;

typedef struct
{
    int source_image;
    int sampler;
} GLTFTexture;

typedef struct
{
    int wrap_s;
    int wrap_t;
    int min_filter;
    int mag_filter;
} GLTFSampler;

typedef struct
{
    int mesh;
    int *children;
    int child_count;
    bool has_matrix;
    float matrix[16];
    float translation[3];
    float rotation[4];
    float scale[3];
} GLTFNode;

typedef struct
{
    int *nodes;
    int node_count;
} GLTFScene;

typedef enum
{
    GLTF_ALPHA_OPAQUE = 0,
    GLTF_ALPHA_MASK = 1,
    GLTF_ALPHA_BLEND = 2
} GLTFAlphaMode;

typedef struct
{
    float base_color[4];
    int base_color_texture;
    float metallic_factor;
    GLTFAlphaMode alpha_mode;
    float alpha_cutoff;
    bool double_sided;
    float emissive_factor[3];
    int emissive_texture;

    float emissive_strength;
} GLTFMaterial;

typedef struct
{
    GLTFBuffer *buffers;
    int buffer_count;
    GLTFBufferView *views;
    int view_count;
    GLTFAccessor *accessors;
    int accessor_count;
    GLTFMesh *meshes;
    int mesh_count;
    GLTFMaterial *materials;
    int material_count;

    GLTFImage *images;
    int image_count;
    GLTFTexture *textures;
    int texture_count;

    GLTFSampler *samplers;
    int sampler_count;

    GLTFNode *nodes;
    int node_count;
    GLTFScene *scenes;
    int scene_count;
    int default_scene;

} GLTFDocument;

static void gltf_document_free(GLTFDocument *doc)
{
    if (!doc)
        return;
    for (int i = 0; i < doc->buffer_count; i++)
    {
        free(doc->buffers[i].uri_data);
        free(doc->buffers[i].uri);
    }
    free(doc->buffers);
    free(doc->views);
    free(doc->accessors);
    if (doc->meshes)
    {
        for (int i = 0; i < doc->mesh_count; i++)
            free(doc->meshes[i].primitives);
        free(doc->meshes);
    }

    if (doc->images)
    {
        for (int i = 0; i < doc->image_count; i++)
        {
            free(doc->images[i].mime_type);
            free(doc->images[i].uri);
        }
        free(doc->images);
    }
    free(doc->textures);
    free(doc->materials);
    free(doc->samplers);
    if (doc->nodes)
    {
        for (int i = 0; i < doc->node_count; i++)
            free(doc->nodes[i].children);
        free(doc->nodes);
    }
    if (doc->scenes)
    {
        for (int i = 0; i < doc->scene_count; i++)
            free(doc->scenes[i].nodes);
        free(doc->scenes);
    }
    free(doc);
}

static GLTFBufferView gltf_get_buffer_view(const GLTFDocument *doc, int index)
{
    if (doc && index >= 0 && index < doc->view_count)
        return doc->views[index];
    GLTFBufferView empty;
    memset(&empty, 0, sizeof(empty));
    return empty;
}

static const unsigned char *gltf_buffer_data(const GLTFDocument *doc, int buffer_index, size_t offset)
{
    if (doc && buffer_index >= 0 && buffer_index < doc->buffer_count)
    {
        if (doc->buffers[buffer_index].uri_data)
        {
            return doc->buffers[buffer_index].uri_data + offset;
        }
    }
    return NULL;
}

static bool gltf_parse_document_from_root(const cJSON *root, GLTFDocument **out_doc)
{
    if (!root || !cJSON_IsObject(root))
        return false;
    GLTFDocument *doc = (GLTFDocument *)calloc(1, sizeof(GLTFDocument));
    if (!doc)
        return false;
    doc->default_scene = -1;

    cJSON *buffers_val = cJSON_GetObjectItemCaseSensitive(root, "buffers");
    if (buffers_val && cJSON_IsArray(buffers_val))
    {
        doc->buffer_count = (int)cJSON_GetArraySize(buffers_val);
        doc->buffers = (GLTFBuffer *)calloc(doc->buffer_count, sizeof(GLTFBuffer));
        for (int i = 0; i < doc->buffer_count; i++)
        {
            cJSON *b_val = cJSON_GetArrayItem(buffers_val, i);
            GLTFBuffer b;
            memset(&b, 0, sizeof(b));
            if (b_val && cJSON_IsObject(b_val))
            {
                cJSON *bl = cJSON_GetObjectItemCaseSensitive(b_val, "byteLength");
                if (bl)
                    b.byte_length = (size_t)bl->valuedouble;
                cJSON *uri = cJSON_GetObjectItemCaseSensitive(b_val, "uri");
                if (uri && cJSON_IsString(uri))
                    b.uri = strdup(uri->valuestring);
            }
            doc->buffers[i] = b;
        }
    }

    cJSON *views_val = cJSON_GetObjectItemCaseSensitive(root, "bufferViews");
    if (views_val && cJSON_IsArray(views_val))
    {
        doc->view_count = (int)cJSON_GetArraySize(views_val);
        doc->views = (GLTFBufferView *)calloc(doc->view_count, sizeof(GLTFBufferView));
        for (int i = 0; i < doc->view_count; i++)
        {
            cJSON *v_val = cJSON_GetArrayItem(views_val, i);
            GLTFBufferView view;
            memset(&view, 0, sizeof(view));
            if (v_val && cJSON_IsObject(v_val))
            {
                cJSON *vv;
                vv = cJSON_GetObjectItemCaseSensitive(v_val, "buffer");
                if (vv)
                    view.buffer = (int)vv->valuedouble;
                vv = cJSON_GetObjectItemCaseSensitive(v_val, "byteLength");
                if (vv)
                    view.byte_length = (size_t)vv->valuedouble;
                vv = cJSON_GetObjectItemCaseSensitive(v_val, "byteOffset");
                if (vv)
                    view.byte_offset = (size_t)vv->valuedouble;
                vv = cJSON_GetObjectItemCaseSensitive(v_val, "byteStride");
                if (vv)
                    view.byte_stride = (size_t)vv->valuedouble;
            }
            doc->views[i] = view;
        }
    }

    cJSON *accessors_val = cJSON_GetObjectItemCaseSensitive(root, "accessors");
    if (accessors_val && cJSON_IsArray(accessors_val))
    {
        doc->accessor_count = (int)cJSON_GetArraySize(accessors_val);
        doc->accessors = (GLTFAccessor *)calloc(doc->accessor_count, sizeof(GLTFAccessor));
        for (int i = 0; i < doc->accessor_count; i++)
        {
            cJSON *acc_val = cJSON_GetArrayItem(accessors_val, i);
            GLTFAccessor acc;
            memset(&acc, 0, sizeof(acc));
            if (acc_val && cJSON_IsObject(acc_val))
            {
                cJSON *av;
                av = cJSON_GetObjectItemCaseSensitive(acc_val, "bufferView");
                if (av)
                    acc.buffer_view = (int)av->valuedouble;
                av = cJSON_GetObjectItemCaseSensitive(acc_val, "componentType");
                if (av)
                    acc.component_type = (int)av->valuedouble;
                av = cJSON_GetObjectItemCaseSensitive(acc_val, "type");
                if (av && cJSON_IsString(av))
                {
                    if (strcmp(av->valuestring, "SCALAR") == 0)
                        acc.type = 1;
                    else if (strcmp(av->valuestring, "VEC2") == 0)
                        acc.type = 2;
                    else if (strcmp(av->valuestring, "VEC3") == 0)
                        acc.type = 3;
                    else if (strcmp(av->valuestring, "VEC4") == 0)
                        acc.type = 4;
                }
                av = cJSON_GetObjectItemCaseSensitive(acc_val, "count");
                if (av)
                    acc.count = (size_t)av->valuedouble;
                av = cJSON_GetObjectItemCaseSensitive(acc_val, "byteOffset");
                if (av)
                    acc.byte_offset = (size_t)av->valuedouble;
                av = cJSON_GetObjectItemCaseSensitive(acc_val, "normalized");
                if (av && cJSON_IsBool(av))
                    acc.normalized = cJSON_IsTrue(av);
                av = cJSON_GetObjectItemCaseSensitive(acc_val, "max");
                if (av && cJSON_IsArray(av) && cJSON_GetArraySize(av) >= 3)
                {
                    acc.max[0] = (float)cJSON_GetArrayItem(av, 0)->valuedouble;
                    acc.max[1] = (float)cJSON_GetArrayItem(av, 1)->valuedouble;
                    acc.max[2] = (float)cJSON_GetArrayItem(av, 2)->valuedouble;
                }
                av = cJSON_GetObjectItemCaseSensitive(acc_val, "min");
                if (av && cJSON_IsArray(av) && cJSON_GetArraySize(av) >= 3)
                {
                    acc.min[0] = (float)cJSON_GetArrayItem(av, 0)->valuedouble;
                    acc.min[1] = (float)cJSON_GetArrayItem(av, 1)->valuedouble;
                    acc.min[2] = (float)cJSON_GetArrayItem(av, 2)->valuedouble;
                }
            }
            doc->accessors[i] = acc;
        }
    }

    cJSON *meshes_val = cJSON_GetObjectItemCaseSensitive(root, "meshes");
    if (meshes_val && cJSON_IsArray(meshes_val))
    {
        doc->mesh_count = (int)cJSON_GetArraySize(meshes_val);
        doc->meshes = (GLTFMesh *)calloc(doc->mesh_count, sizeof(GLTFMesh));
        for (int i = 0; i < doc->mesh_count; i++)
        {
            cJSON *mesh_val = cJSON_GetArrayItem(meshes_val, i);
            GLTFMesh mesh;
            memset(&mesh, 0, sizeof(mesh));
            if (mesh_val && cJSON_IsObject(mesh_val))
            {
                cJSON *prims_val = cJSON_GetObjectItemCaseSensitive(mesh_val, "primitives");
                if (prims_val && cJSON_IsArray(prims_val))
                {
                    mesh.primitive_count = (int)cJSON_GetArraySize(prims_val);
                    mesh.primitives = (GLTFPrimitive *)calloc(mesh.primitive_count, sizeof(GLTFPrimitive));
                    for (int j = 0; j < mesh.primitive_count; j++)
                    {
                        cJSON *prim_val = cJSON_GetArrayItem(prims_val, j);
                        GLTFPrimitive prim;
                        memset(&prim, 0, sizeof(prim));
                        prim.pos_accessor = -1;
                        prim.norm_accessor = -1;
                        prim.uv_accessor = -1;
                        prim.material = -1;

                        prim.mode = 4;
                        if (prim_val && cJSON_IsObject(prim_val))
                        {
                            cJSON *av;
                            av = cJSON_GetObjectItemCaseSensitive(prim_val, "mode");
                            if (av)
                                prim.mode = (int)av->valuedouble;
                            av = cJSON_GetObjectItemCaseSensitive(prim_val, "indices");
                            if (av && cJSON_IsNumber(av))
                                prim.indices_accessor = (int)av->valuedouble;
                            av = cJSON_GetObjectItemCaseSensitive(prim_val, "material");
                            if (av && cJSON_IsNumber(av))
                                prim.material = (int)av->valuedouble;
                            cJSON *attrs = cJSON_GetObjectItemCaseSensitive(prim_val, "attributes");
                            if (attrs && cJSON_IsObject(attrs))
                            {
                                cJSON *pos_val = cJSON_GetObjectItemCaseSensitive(attrs, "POSITION");
                                if (pos_val && cJSON_IsNumber(pos_val))
                                    prim.pos_accessor = (int)pos_val->valuedouble;
                                cJSON *norm_val = cJSON_GetObjectItemCaseSensitive(attrs, "NORMAL");
                                if (norm_val && cJSON_IsNumber(norm_val))
                                    prim.norm_accessor = (int)norm_val->valuedouble;
                                cJSON *uv_val = cJSON_GetObjectItemCaseSensitive(attrs, "TEXCOORD_0");
                                if (uv_val && cJSON_IsNumber(uv_val))
                                    prim.uv_accessor = (int)uv_val->valuedouble;
                            }
                        }
                        mesh.primitives[j] = prim;
                    }
                }
            }
            doc->meshes[i] = mesh;
        }
    }

    cJSON *images_val = cJSON_GetObjectItemCaseSensitive(root, "images");
    if (images_val && cJSON_IsArray(images_val))
    {
        doc->image_count = (int)cJSON_GetArraySize(images_val);
        doc->images = (GLTFImage *)calloc(doc->image_count, sizeof(GLTFImage));
        for (int i = 0; i < doc->image_count; i++)
        {

            doc->images[i].buffer_view = -1;
            cJSON *img_val = cJSON_GetArrayItem(images_val, i);
            if (img_val && cJSON_IsObject(img_val))
            {
                cJSON *bv = cJSON_GetObjectItemCaseSensitive(img_val, "bufferView");
                if (bv)
                    doc->images[i].buffer_view = (int)bv->valuedouble;
                cJSON *mt = cJSON_GetObjectItemCaseSensitive(img_val, "mimeType");
                if (mt && cJSON_IsString(mt))
                    doc->images[i].mime_type = strdup(mt->valuestring);
                cJSON *uri = cJSON_GetObjectItemCaseSensitive(img_val, "uri");
                if (uri && cJSON_IsString(uri))
                    doc->images[i].uri = strdup(uri->valuestring);
            }
        }
    }

    cJSON *textures_val = cJSON_GetObjectItemCaseSensitive(root, "textures");
    if (textures_val && cJSON_IsArray(textures_val))
    {
        doc->texture_count = (int)cJSON_GetArraySize(textures_val);
        doc->textures = (GLTFTexture *)calloc(doc->texture_count, sizeof(GLTFTexture));
        for (int i = 0; i < doc->texture_count; i++)
        {
            doc->textures[i].source_image = -1;
            doc->textures[i].sampler = -1;
            cJSON *tex_val = cJSON_GetArrayItem(textures_val, i);
            if (tex_val && cJSON_IsObject(tex_val))
            {
                cJSON *src = cJSON_GetObjectItemCaseSensitive(tex_val, "source");
                if (src)
                    doc->textures[i].source_image = (int)src->valuedouble;
                cJSON *samp = cJSON_GetObjectItemCaseSensitive(tex_val, "sampler");
                if (samp && cJSON_IsNumber(samp))
                    doc->textures[i].sampler = (int)samp->valuedouble;
            }
        }
    }

    cJSON *samplers_val = cJSON_GetObjectItemCaseSensitive(root, "samplers");
    if (samplers_val && cJSON_IsArray(samplers_val))
    {
        doc->sampler_count = (int)cJSON_GetArraySize(samplers_val);
        doc->samplers = (GLTFSampler *)calloc(doc->sampler_count, sizeof(GLTFSampler));
        for (int i = 0; i < doc->sampler_count; i++)
        {
            cJSON *s_val = cJSON_GetArrayItem(samplers_val, i);
            GLTFSampler samp;
            samp.wrap_s = 10497;
            samp.wrap_t = 10497;
            samp.min_filter = 9729;
            samp.mag_filter = 9729;
            if (s_val && cJSON_IsObject(s_val))
            {
                cJSON *sv;
                sv = cJSON_GetObjectItemCaseSensitive(s_val, "wrapS");
                if (sv && cJSON_IsNumber(sv))
                    samp.wrap_s = (int)sv->valuedouble;
                sv = cJSON_GetObjectItemCaseSensitive(s_val, "wrapT");
                if (sv && cJSON_IsNumber(sv))
                    samp.wrap_t = (int)sv->valuedouble;
                sv = cJSON_GetObjectItemCaseSensitive(s_val, "minFilter");
                if (sv && cJSON_IsNumber(sv))
                    samp.min_filter = (int)sv->valuedouble;
                sv = cJSON_GetObjectItemCaseSensitive(s_val, "magFilter");
                if (sv && cJSON_IsNumber(sv))
                    samp.mag_filter = (int)sv->valuedouble;
            }
            doc->samplers[i] = samp;
        }
    }

    cJSON *materials_val = cJSON_GetObjectItemCaseSensitive(root, "materials");
    if (materials_val && cJSON_IsArray(materials_val))
    {
        doc->material_count = (int)cJSON_GetArraySize(materials_val);
        doc->materials = (GLTFMaterial *)calloc(doc->material_count, sizeof(GLTFMaterial));
        for (int i = 0; i < doc->material_count; i++)
        {
            cJSON *mat_val = cJSON_GetArrayItem(materials_val, i);
            GLTFMaterial mat;
            memset(&mat, 0, sizeof(mat));
            mat.base_color[0] = mat.base_color[1] = mat.base_color[2] = 0.8f;
            mat.base_color[3] = 1.0f;
            mat.base_color_texture = -1;

            mat.metallic_factor = 1.0f;

            mat.alpha_mode = GLTF_ALPHA_OPAQUE;
            mat.alpha_cutoff = 0.5f;
            mat.double_sided = false;

            mat.emissive_factor[0] = 0.0f;
            mat.emissive_factor[1] = 0.0f;
            mat.emissive_factor[2] = 0.0f;
            mat.emissive_texture = -1;
            mat.emissive_strength = 1.0f;
            if (mat_val && cJSON_IsObject(mat_val))
            {
                cJSON *pbr = cJSON_GetObjectItemCaseSensitive(mat_val, "pbrMetallicRoughness");
                if (pbr && cJSON_IsObject(pbr))
                {
                    cJSON *bc = cJSON_GetObjectItemCaseSensitive(pbr, "baseColorFactor");
                    if (bc && cJSON_IsArray(bc) && cJSON_GetArraySize(bc) >= 4)
                    {
                        mat.base_color[0] = (float)cJSON_GetArrayItem(bc, 0)->valuedouble;
                        mat.base_color[1] = (float)cJSON_GetArrayItem(bc, 1)->valuedouble;
                        mat.base_color[2] = (float)cJSON_GetArrayItem(bc, 2)->valuedouble;
                        mat.base_color[3] = (float)cJSON_GetArrayItem(bc, 3)->valuedouble;
                    }
                    else
                    {

                        mat.base_color[0] = 1.0f;
                        mat.base_color[1] = 1.0f;
                        mat.base_color[2] = 1.0f;
                        mat.base_color[3] = 1.0f;
                    }

                    cJSON *bct = cJSON_GetObjectItemCaseSensitive(pbr, "baseColorTexture");
                    if (bct && cJSON_IsObject(bct))
                    {
                        cJSON *idx = cJSON_GetObjectItemCaseSensitive(bct, "index");
                        mat.base_color_texture = idx ? (int)idx->valuedouble : -1;
                    }
                    else
                    {
                        mat.base_color_texture = -1;
                    }

                    cJSON *mf = cJSON_GetObjectItemCaseSensitive(pbr, "metallicFactor");
                    if (mf && cJSON_IsNumber(mf))
                        mat.metallic_factor = (float)mf->valuedouble;
                }
                else
                {
                    mat.base_color[0] = 0.8f;
                    mat.base_color[1] = 0.8f;
                    mat.base_color[2] = 0.8f;
                    mat.base_color[3] = 1.0f;
                    mat.base_color_texture = -1;

                    mat.metallic_factor = 0.0f;
                }

                cJSON *am = cJSON_GetObjectItemCaseSensitive(mat_val, "alphaMode");
                if (am && cJSON_IsString(am))
                {
                    if (strcmp(am->valuestring, "BLEND") == 0)
                        mat.alpha_mode = GLTF_ALPHA_BLEND;
                    else if (strcmp(am->valuestring, "MASK") == 0)
                        mat.alpha_mode = GLTF_ALPHA_MASK;
                    else
                        mat.alpha_mode = GLTF_ALPHA_OPAQUE;
                }

                cJSON *ac = cJSON_GetObjectItemCaseSensitive(mat_val, "alphaCutoff");
                if (ac && cJSON_IsNumber(ac))
                    mat.alpha_cutoff = (float)ac->valuedouble;

                cJSON *ds = cJSON_GetObjectItemCaseSensitive(mat_val, "doubleSided");
                if (ds && cJSON_IsBool(ds))
                    mat.double_sided = cJSON_IsTrue(ds);

                cJSON *ef = cJSON_GetObjectItemCaseSensitive(mat_val, "emissiveFactor");
                if (ef && cJSON_IsArray(ef) && cJSON_GetArraySize(ef) >= 3)
                {
                    mat.emissive_factor[0] = (float)cJSON_GetArrayItem(ef, 0)->valuedouble;
                    mat.emissive_factor[1] = (float)cJSON_GetArrayItem(ef, 1)->valuedouble;
                    mat.emissive_factor[2] = (float)cJSON_GetArrayItem(ef, 2)->valuedouble;
                }

                cJSON *et = cJSON_GetObjectItemCaseSensitive(mat_val, "emissiveTexture");
                if (et && cJSON_IsObject(et))
                {
                    cJSON *idx = cJSON_GetObjectItemCaseSensitive(et, "index");
                    if (idx)
                        mat.emissive_texture = (int)idx->valuedouble;
                }

                cJSON *ext = cJSON_GetObjectItemCaseSensitive(mat_val, "extensions");
                if (ext && cJSON_IsObject(ext))
                {
                    cJSON *ems = cJSON_GetObjectItemCaseSensitive(ext, "KHR_materials_emissive_strength");
                    if (ems && cJSON_IsObject(ems))
                    {
                        cJSON *strength = cJSON_GetObjectItemCaseSensitive(ems, "emissiveStrength");
                        if (strength && cJSON_IsNumber(strength))
                            mat.emissive_strength = (float)strength->valuedouble;
                    }
                }
            }
            doc->materials[i] = mat;
        }
    }

    cJSON *nodes_val = cJSON_GetObjectItemCaseSensitive(root, "nodes");
    if (nodes_val && cJSON_IsArray(nodes_val))
    {
        doc->node_count = (int)cJSON_GetArraySize(nodes_val);
        doc->nodes = (GLTFNode *)calloc(doc->node_count, sizeof(GLTFNode));
        for (int i = 0; i < doc->node_count; i++)
        {
            cJSON *n_val = cJSON_GetArrayItem(nodes_val, i);
            GLTFNode node;
            memset(&node, 0, sizeof(node));
            node.mesh = -1;
            node.rotation[3] = 1.0f;
            node.scale[0] = node.scale[1] = node.scale[2] = 1.0f;
            if (n_val && cJSON_IsObject(n_val))
            {
                cJSON *mv = cJSON_GetObjectItemCaseSensitive(n_val, "mesh");
                if (mv && cJSON_IsNumber(mv))
                    node.mesh = (int)mv->valuedouble;

                cJSON *matrix_val = cJSON_GetObjectItemCaseSensitive(n_val, "matrix");
                if (matrix_val && cJSON_IsArray(matrix_val) && cJSON_GetArraySize(matrix_val) == 16)
                {
                    node.has_matrix = true;
                    for (int k = 0; k < 16; k++)
                        node.matrix[k] = (float)cJSON_GetArrayItem(matrix_val, k)->valuedouble;
                }
                else
                {

                    cJSON *t = cJSON_GetObjectItemCaseSensitive(n_val, "translation");
                    if (t && cJSON_IsArray(t) && cJSON_GetArraySize(t) >= 3)
                        for (int k = 0; k < 3; k++)
                            node.translation[k] = (float)cJSON_GetArrayItem(t, k)->valuedouble;
                    cJSON *r = cJSON_GetObjectItemCaseSensitive(n_val, "rotation");
                    if (r && cJSON_IsArray(r) && cJSON_GetArraySize(r) >= 4)
                        for (int k = 0; k < 4; k++)
                            node.rotation[k] = (float)cJSON_GetArrayItem(r, k)->valuedouble;
                    cJSON *s = cJSON_GetObjectItemCaseSensitive(n_val, "scale");
                    if (s && cJSON_IsArray(s) && cJSON_GetArraySize(s) >= 3)
                        for (int k = 0; k < 3; k++)
                            node.scale[k] = (float)cJSON_GetArrayItem(s, k)->valuedouble;
                }

                cJSON *children_val = cJSON_GetObjectItemCaseSensitive(n_val, "children");
                if (children_val && cJSON_IsArray(children_val))
                {
                    node.child_count = (int)cJSON_GetArraySize(children_val);
                    node.children = (int *)calloc(node.child_count, sizeof(int));
                    for (int k = 0; k < node.child_count; k++)
                        node.children[k] = (int)cJSON_GetArrayItem(children_val, k)->valuedouble;
                }
            }
            doc->nodes[i] = node;
        }
    }

    cJSON *scene_idx_val = cJSON_GetObjectItemCaseSensitive(root, "scene");
    if (scene_idx_val && cJSON_IsNumber(scene_idx_val))
        doc->default_scene = (int)scene_idx_val->valuedouble;

    cJSON *scenes_val = cJSON_GetObjectItemCaseSensitive(root, "scenes");
    if (scenes_val && cJSON_IsArray(scenes_val))
    {
        doc->scene_count = (int)cJSON_GetArraySize(scenes_val);
        doc->scenes = (GLTFScene *)calloc(doc->scene_count, sizeof(GLTFScene));
        for (int i = 0; i < doc->scene_count; i++)
        {
            cJSON *sc_val = cJSON_GetArrayItem(scenes_val, i);
            GLTFScene sc;
            memset(&sc, 0, sizeof(sc));
            if (sc_val && cJSON_IsObject(sc_val))
            {
                cJSON *sc_nodes = cJSON_GetObjectItemCaseSensitive(sc_val, "nodes");
                if (sc_nodes && cJSON_IsArray(sc_nodes))
                {
                    sc.node_count = (int)cJSON_GetArraySize(sc_nodes);
                    sc.nodes = (int *)calloc(sc.node_count, sizeof(int));
                    for (int k = 0; k < sc.node_count; k++)
                        sc.nodes[k] = (int)cJSON_GetArrayItem(sc_nodes, k)->valuedouble;
                }
            }
            doc->scenes[i] = sc;
        }
    }
    if (doc->default_scene < 0 && doc->scene_count > 0)
        doc->default_scene = 0;

    *out_doc = doc;
    return true;
}

static bool gltf_parse_document(const char *json_text, GLTFDocument **out_doc)
{
    cJSON *root = cJSON_Parse(json_text);
    if (!root || !cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return false;
    }
    bool ok = gltf_parse_document_from_root(root, out_doc);
    cJSON_Delete(root);
    return ok;
}

static int b64_char_value(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static unsigned char *base64_decode(const char *in, size_t in_len, size_t *out_len)
{
    unsigned char *out = (unsigned char *)malloc((in_len / 4 + 1) * 3 + 4);
    if (!out)
        return NULL;
    size_t out_pos = 0;
    int vals[4];
    int nvals = 0;
    for (size_t i = 0; i < in_len; i++)
    {
        unsigned char c = (unsigned char)in[i];
        if (c == '=' || c == '\0')
            break;
        int v = b64_char_value(c);
        if (v < 0)
            continue;
        vals[nvals++] = v;
        if (nvals == 4)
        {
            out[out_pos++] = (unsigned char)((vals[0] << 2) | (vals[1] >> 4));
            out[out_pos++] = (unsigned char)(((vals[1] & 0xF) << 4) | (vals[2] >> 2));
            out[out_pos++] = (unsigned char)(((vals[2] & 0x3) << 6) | vals[3]);
            nvals = 0;
        }
    }
    if (nvals >= 2)
    {
        out[out_pos++] = (unsigned char)((vals[0] << 2) | (vals[1] >> 4));
        if (nvals == 3)
            out[out_pos++] = (unsigned char)(((vals[1] & 0xF) << 4) | (vals[2] >> 2));
    }
    *out_len = out_pos;
    return out;
}

static bool is_data_uri(const char *uri)
{
    return uri && strncmp(uri, "data:", 5) == 0;
}

static unsigned char *decode_data_uri(const char *uri, size_t *out_size)
{
    const char *comma = strchr(uri, ',');
    if (!comma)
        return NULL;
    bool is_base64 = false;
    for (const char *p = uri + 5; p < comma; p++)
    {
        if (strncmp(p, ";base64", 7) == 0)
        {
            is_base64 = true;
            break;
        }
    }
    const char *payload = comma + 1;
    size_t payload_len = strlen(payload);
    if (is_base64)
        return base64_decode(payload, payload_len, out_size);

    unsigned char *out = (unsigned char *)malloc(payload_len + 1);
    if (!out)
        return NULL;
    size_t out_pos = 0;
    for (size_t i = 0; i < payload_len; i++)
    {
        if (payload[i] == '%' && i + 2 < payload_len)
        {
            char hex[3] = {payload[i + 1], payload[i + 2], 0};
            out[out_pos++] = (unsigned char)strtol(hex, NULL, 16);
            i += 2;
        }
        else
        {
            out[out_pos++] = (unsigned char)payload[i];
        }
    }
    *out_size = out_pos;
    return out;
}

static char *percent_decode_alloc(const char *s)
{
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    size_t out_pos = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (s[i] == '%' && i + 2 < len)
        {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            out[out_pos++] = (char)strtol(hex, NULL, 16);
            i += 2;
        }
        else
        {
            out[out_pos++] = s[i];
        }
    }
    out[out_pos] = '\0';
    return out;
}

static char *join_path_alloc(const char *base_dir, const char *rel)
{
    if (!base_dir || base_dir[0] == '\0')
        return strdup(rel);
    size_t base_len = strlen(base_dir);
    bool needs_sep = base_len > 0 && base_dir[base_len - 1] != '/';
    size_t total = base_len + (needs_sep ? 1 : 0) + strlen(rel) + 1;
    char *out = (char *)malloc(total);
    if (!out)
        return NULL;
    snprintf(out, total, "%s%s%s", base_dir, needs_sep ? "/" : "", rel);
    return out;
}

static bool resolve_and_load_uri(const char *uri, const char *base_dir, unsigned char **out_data, size_t *out_size)
{
    if (!uri || !out_data || !out_size)
        return false;
    *out_data = NULL;
    *out_size = 0;

    if (is_data_uri(uri))
    {
        size_t size = 0;
        unsigned char *decoded = decode_data_uri(uri, &size);
        if (!decoded)
        {
            LOG_ERROR("3D: failed to decode data URI (len=%zu)", strlen(uri));
            return false;
        }
        *out_data = decoded;
        *out_size = size;
        return true;
    }

    if (!base_dir)
    {
        LOG_ERROR("3D: glTF references external resource '%s' but no base directory is known "
                  "(model was loaded via aroma_3d_load_model_from_memory with no path) - skipping",
                  uri);
        return false;
    }

    char *decoded_uri = percent_decode_alloc(uri);
    if (!decoded_uri)
        return false;
    char *full_path = join_path_alloc(base_dir, decoded_uri);
    free(decoded_uri);
    if (!full_path)
        return false;

    FILE *f = fopen(full_path, "rb");
    if (!f)
    {
        LOG_ERROR("3D: failed to open external glTF resource '%s'", full_path);
        free(full_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0)
    {
        LOG_ERROR("3D: external glTF resource '%s' is empty or unreadable", full_path);
        fclose(f);
        free(full_path);
        return false;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf)
    {
        fclose(f);
        free(full_path);
        return false;
    }
    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size)
    {
        LOG_ERROR("3D: short read on external resource '%s' (%zu/%ld bytes)", full_path, read_bytes, size);
        free(buf);
        free(full_path);
        return false;
    }
    LOG_INFO("3D: loaded external resource '%s' (%zu bytes)", full_path, read_bytes);
    free(full_path);
    *out_data = buf;
    *out_size = read_bytes;
    return true;
}

static bool gltf_load_buffers(GLTFDocument *doc, const unsigned char *bin_data, size_t bin_size, const char *base_dir)
{
    for (int i = 0; i < doc->buffer_count; i++)
    {
        if (doc->buffers[i].byte_length == 0)
            continue;

        if (doc->buffers[i].uri)
        {
            unsigned char *data = NULL;
            size_t size = 0;
            if (resolve_and_load_uri(doc->buffers[i].uri, base_dir, &data, &size))
            {
                if (size < doc->buffers[i].byte_length)
                {
                    LOG_ERROR("3D: buffer[%d] uri='%s' resolved to %zu bytes but byteLength=%zu - "
                              "using what is available",
                              i, doc->buffers[i].uri, size, doc->buffers[i].byte_length);
                }
                doc->buffers[i].uri_data = data;
                doc->buffers[i].uri_size = size;
            }
            continue;
        }

        if (bin_data && doc->buffers[i].byte_length <= bin_size)
        {
            doc->buffers[i].uri_data = (unsigned char *)malloc(doc->buffers[i].byte_length);
            if (doc->buffers[i].uri_data)
            {
                memcpy(doc->buffers[i].uri_data, bin_data, doc->buffers[i].byte_length);
                doc->buffers[i].uri_size = doc->buffers[i].byte_length;
            }
        }
        else if (!bin_data)
        {
            LOG_ERROR("3D: buffer[%d] has no uri and this file has no GLB BIN chunk - "
                      "geometry using this buffer will be missing",
                      i);
        }
    }
    return true;
}

struct Aroma3DMesh
{
    GLuint vao;
    GLuint vbo_positions;
    GLuint vbo_normals;
    GLuint vbo_texcoords;
    GLuint ebo;
    int index_count;
    int vertex_count;
    GLuint texture_id;
    bool has_texture;
    float base_color[3];
    float alpha;

    float metallic;

    float clearcoat;
    GLTFAlphaMode alpha_mode;
    float alpha_cutoff;
    bool double_sided;
    float emissive_factor[3];
    float emissive_strength;
    GLuint emissive_texture_id;
    bool has_emissive_texture;
    GLenum index_type;
    size_t index_offset;

    float world_transform[16];
};

struct Aroma3DModel
{
    Aroma3DMesh meshes[AROMA_3D_MAX_MESHES];
    int mesh_count;
    float bounds_min[3];
    float bounds_max[3];
};

typedef struct
{
    float *positions;
    float *normals;
    float *texcoords;
    unsigned char *indices_raw;
    size_t indices_raw_size;

    size_t pos_len, pos_stride, pos_accessor_offset;
    size_t norm_len, norm_stride, norm_accessor_offset;
    size_t uv_len, uv_stride, uv_accessor_offset;

    int vertex_count;
    int index_count;
    GLenum index_type;
    size_t index_accessor_offset;

    bool has_base_color_pixels;
    unsigned char *base_color_pixels;
    int base_color_w, base_color_h;
    GLint base_color_wrap_s, base_color_wrap_t, base_color_min_filter, base_color_mag_filter;

    bool has_emissive_pixels;
    unsigned char *emissive_pixels;
    int emissive_w, emissive_h;
    GLint emissive_wrap_s, emissive_wrap_t, emissive_min_filter, emissive_mag_filter;

    float base_color[3];
    float alpha;
    float metallic;
    float clearcoat;
    GLTFAlphaMode alpha_mode;
    float alpha_cutoff;
    bool double_sided;
    float emissive_factor[3];
    float emissive_strength;

    float world_transform[16];
} StagedMesh;

typedef struct
{
    StagedMesh meshes[AROMA_3D_MAX_MESHES];
    int mesh_count;
    float bounds_min[3];
    float bounds_max[3];
} StagedModel;

typedef enum
{
    AROMA_3D_LOAD_JOB_RUNNING = 0,
    AROMA_3D_LOAD_JOB_SUCCESS,
    AROMA_3D_LOAD_JOB_FAILED,
    AROMA_3D_LOAD_JOB_CONSUMED
} Aroma3DLoadJobStatus;

typedef struct Aroma3DLoadJob Aroma3DLoadJob;

struct Aroma3DLoadJob
{
    pthread_t thread;
    bool thread_started;

    unsigned char *input_data;
    size_t input_size;
    char *input_base_dir;

    StagedModel *staged;
    bool parse_ok;

    _Atomic(int) status;
};

static GLuint g_3d_shader_program = 0;
static GLint g_u_model = -1, g_u_view = -1, g_u_proj = -1, g_u_normal_matrix = -1;
static GLint g_u_base_color = -1, g_u_light_pos = -1, g_u_has_texture = -1, g_u_base_tex = -1;
static GLint g_u_eye_pos = -1;
static GLint g_u_alpha = -1, g_u_alpha_mode = -1, g_u_alpha_cutoff = -1;
static GLint g_u_emissive_factor = -1, g_u_emissive_strength = -1;
static GLint g_u_emissive_tex = -1, g_u_has_emissive_texture = -1;
static GLint g_u_metallic = -1;
static GLint g_u_clearcoat = -1;
static bool g_3d_initialized = false;

static vec3 g_3d_light_position = {2.0f, 4.0f, 1.2f};

static const char *g_vert_shader =
    "#version 300 es\n"
    "precision mediump float;\n"
    "layout(location=0) in vec3 a_position;\n"
    "layout(location=1) in vec3 a_normal;\n"
    "layout(location=2) in vec2 a_texcoord;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_proj;\n"
    "uniform mat3 u_normal_matrix;\n"
    "out vec3 v_normal;\n"
    "out vec2 v_texcoord;\n"
    "out vec3 v_world_pos;\n"
    "void main() {\n"
    "    vec4 world = u_model * vec4(a_position, 1.0);\n"
    "    v_normal = normalize(u_normal_matrix * a_normal);\n"
    "    v_texcoord = a_texcoord;\n"
    "    v_world_pos = world.xyz;\n"
    "    gl_Position = u_proj * u_view * world;\n"
    "}\n";

static const char *g_frag_shader =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec3 v_normal;\n"
    "in vec2 v_texcoord;\n"
    "in vec3 v_world_pos;\n"
    "uniform vec3 u_base_color;\n"
    "uniform float u_alpha;\n"
    "uniform int u_alpha_mode;\n"
    "uniform float u_alpha_cutoff;\n"
    "uniform vec3 u_light_pos;\n"
    "uniform vec3 u_eye_pos;\n"
    "uniform sampler2D u_base_tex;\n"
    "uniform bool u_has_texture;\n"
    "uniform vec3 u_emissive_factor;\n"
    "uniform float u_emissive_strength;\n"
    "uniform sampler2D u_emissive_tex;\n"
    "uniform bool u_has_emissive_texture;\n"
    "uniform float u_metallic;\n"
    "uniform float u_clearcoat;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec3 color = u_base_color;\n"
    "    float tex_alpha = 1.0;\n"
    "    if (u_has_texture) {\n"
    "        vec4 tex_sample = texture(u_base_tex, v_texcoord);\n"
    "        color *= tex_sample.rgb;\n"
    "        tex_alpha = tex_sample.a;\n"
    "    }\n"
    "    float out_alpha = u_alpha * tex_alpha;\n"
    "    if (u_alpha_mode == 1 && out_alpha < u_alpha_cutoff) discard;\n"
    "    vec3 N = normalize(v_normal);\n"
    "    vec3 L = normalize(u_light_pos - v_world_pos);\n"
    "    vec3 V = normalize(u_eye_pos - v_world_pos);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float diff = max(dot(N, L), 0.0);\n"

    "    float shininess = mix(48.0, 220.0, u_metallic);\n"
    "    float spec = (diff > 0.0) ? pow(max(dot(N, H), 0.0), shininess) : 0.0;\n"
    "    vec3 ambient = (0.6 * color) * (1.0 - u_metallic);\n"
    "    vec3 diffuse = (diff * color) * (1.0 - u_metallic);\n"

    "    vec3 specular = spec * mix(vec3(0.6), color, u_metallic);\n"
    "    vec3 emissive = u_emissive_factor * u_emissive_strength;\n"
    "    if (u_has_emissive_texture) emissive *= texture(u_emissive_tex, v_texcoord).rgb;\n"
    "    vec3 R = reflect(-V, N);\n"
    "    float env_y = R.y * 0.5 + 0.5;\n"

    "    vec3 env_color = mix(vec3(0.10, 0.11, 0.13), vec3(1.3, 1.32, 1.38), pow(env_y, 1.6));\n"
    "    float band = 1.0 - abs(R.x * 0.7 + R.z * 0.3);\n"
    "    float grazing = 1.0 - abs(N.y);\n"
    "    float streak = pow(clamp(band, 0.0, 1.0), mix(60.0, 24.0, grazing));\n"
    "    env_color += vec3(1.6, 1.55, 1.5) * streak;\n"

    "    vec3 reflection_tint = mix(vec3(1.0), color, u_metallic);\n"

    "    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);\n"
    "    vec3 reflected = env_color * reflection_tint * (0.5 + 0.5 * spec);\n"
    "    float reflection_strength = mix(0.45, 1.05, u_metallic) + fresnel * mix(0.15, 0.65, u_metallic);\n"
    "    vec3 final_color = ambient + diffuse + specular + emissive + reflected * reflection_strength;\n"

    "    float cc_shininess = 300.0;\n"
    "    float cc_spec = (diff > 0.0) ? pow(max(dot(N, H), 0.0), cc_shininess) : 0.0;\n"
    "    vec3 clearcoat_specular = vec3(cc_spec) * u_clearcoat;\n"
    "    vec3 clearcoat_reflection = env_color * (0.06 + 0.5 * fresnel) * u_clearcoat;\n"
    "    final_color += clearcoat_specular + clearcoat_reflection;\n"
    "    float final_alpha = (u_alpha_mode == 2) ? max(out_alpha, 0.72) : 1.0;\n"
    "    frag_color = vec4(final_color, final_alpha);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        LOG_ERROR("3D shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool init_3d_resources(void)
{
    if (g_3d_initialized)
        return true;
    GLuint vs = compile_shader(GL_VERTEX_SHADER, g_vert_shader);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, g_frag_shader);
    if (vs == 0 || fs == 0)
    {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return false;
    }
    g_3d_shader_program = glCreateProgram();
    glAttachShader(g_3d_shader_program, vs);
    glAttachShader(g_3d_shader_program, fs);
    glLinkProgram(g_3d_shader_program);
    GLint linked = 0;
    glGetProgramiv(g_3d_shader_program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512];
        glGetProgramInfoLog(g_3d_shader_program, sizeof(log), NULL, log);
        LOG_ERROR("3D shader link failed: %s", log);
        glDeleteProgram(g_3d_shader_program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        g_3d_shader_program = 0;
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    g_u_model = glGetUniformLocation(g_3d_shader_program, "u_model");
    g_u_view = glGetUniformLocation(g_3d_shader_program, "u_view");
    g_u_proj = glGetUniformLocation(g_3d_shader_program, "u_proj");
    g_u_normal_matrix = glGetUniformLocation(g_3d_shader_program, "u_normal_matrix");
    g_u_base_color = glGetUniformLocation(g_3d_shader_program, "u_base_color");
    g_u_light_pos = glGetUniformLocation(g_3d_shader_program, "u_light_pos");
    g_u_eye_pos = glGetUniformLocation(g_3d_shader_program, "u_eye_pos");
    g_u_has_texture = glGetUniformLocation(g_3d_shader_program, "u_has_texture");
    g_u_base_tex = glGetUniformLocation(g_3d_shader_program, "u_base_tex");
    g_u_alpha = glGetUniformLocation(g_3d_shader_program, "u_alpha");
    g_u_alpha_mode = glGetUniformLocation(g_3d_shader_program, "u_alpha_mode");
    g_u_alpha_cutoff = glGetUniformLocation(g_3d_shader_program, "u_alpha_cutoff");
    g_u_emissive_factor = glGetUniformLocation(g_3d_shader_program, "u_emissive_factor");
    g_u_emissive_strength = glGetUniformLocation(g_3d_shader_program, "u_emissive_strength");
    g_u_emissive_tex = glGetUniformLocation(g_3d_shader_program, "u_emissive_tex");
    g_u_has_emissive_texture = glGetUniformLocation(g_3d_shader_program, "u_has_emissive_texture");
    g_u_metallic = glGetUniformLocation(g_3d_shader_program, "u_metallic");
    g_u_clearcoat = glGetUniformLocation(g_3d_shader_program, "u_clearcoat");
    g_3d_initialized = true;
    LOG_INFO("3D rendering resources initialized");
    return true;
}

static void destroy_mesh(Aroma3DMesh *mesh)
{
    if (!mesh)
        return;
    if (mesh->vao)
        glDeleteVertexArrays(1, &mesh->vao);
    if (mesh->vbo_positions)
        glDeleteBuffers(1, &mesh->vbo_positions);
    if (mesh->vbo_normals)
        glDeleteBuffers(1, &mesh->vbo_normals);
    if (mesh->vbo_texcoords)
        glDeleteBuffers(1, &mesh->vbo_texcoords);
    if (mesh->ebo)
        glDeleteBuffers(1, &mesh->ebo);
    if (mesh->texture_id)
        glDeleteTextures(1, &mesh->texture_id);
    if (mesh->emissive_texture_id)
        glDeleteTextures(1, &mesh->emissive_texture_id);
    memset(mesh, 0, sizeof(Aroma3DMesh));
}

static bool upload_mesh_attr(GLuint *vbo, const GLTFDocument *doc, const GLTFBufferView *view,
                             const GLTFAccessor *acc, int components)
{
    if (!view || !acc || view->byte_length == 0)
        return false;
    size_t stride = view->byte_stride > 0 ? view->byte_stride : (size_t)components * 4;
    const unsigned char *data = gltf_buffer_data(doc, (int)view->buffer, view->byte_offset);
    if (!data)
        return false;
    glGenBuffers(1, vbo);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, view->byte_length, data, GL_STATIC_DRAW);
    return true;
}

static bool load_and_upload_texture(const GLTFDocument *doc, int texture_index,
                                    GLuint *out_texture_id, const char *context_label,
                                    const char *base_dir)
{
    if (texture_index < 0 || texture_index >= doc->texture_count)
        return false;

    GLTFTexture *tex = &doc->textures[texture_index];
    if (tex->source_image < 0 || tex->source_image >= doc->image_count)
    {
        LOG_ERROR("build_mesh: %s texture[%d].source_image=%d out of range (image_count=%d)",
                  context_label, texture_index, tex->source_image, doc->image_count);
        return false;
    }

    GLTFImage *img = &doc->images[tex->source_image];

    const unsigned char *img_data = NULL;
    size_t img_size = 0;
    unsigned char *uri_owned_data = NULL;

    if (img->buffer_view >= 0 && img->buffer_view < doc->view_count)
    {
        GLTFBufferView *v = &doc->views[img->buffer_view];
        img_data = gltf_buffer_data(doc, v->buffer, v->byte_offset);
        img_size = v->byte_length;
        if (!img_data)
        {
            LOG_ERROR("build_mesh: %s gltf_buffer_data returned NULL for image bufferView=%d",
                      context_label, img->buffer_view);
        }
    }

    if (!img_data && img->uri)
    {
        if (!resolve_and_load_uri(img->uri, base_dir, &uri_owned_data, &img_size))
        {
            LOG_ERROR("build_mesh: %s image[%d] failed to resolve uri (see prior log line)",
                      context_label, tex->source_image);
            return false;
        }
        img_data = uri_owned_data;
    }

    if (!img_data)
    {
        LOG_ERROR("build_mesh: %s image[%d] has neither a valid bufferView (%d, view_count=%d) "
                  "nor a uri - no pixel data available",
                  context_label, tex->source_image, img->buffer_view, doc->view_count);
        return false;
    }

    int w, h, channels;
    unsigned char *pixels = stbi_load_from_memory(img_data, (int)img_size, &w, &h, &channels, 4);
    free(uri_owned_data);
    if (!pixels)
    {
        LOG_ERROR("build_mesh: %s stbi_load_from_memory failed to decode image "
                  "(source_image=%d, byte_length=%zu) - corrupt or unsupported format?",
                  context_label, tex->source_image, img_size);
        return false;
    }

    glGenTextures(1, out_texture_id);
    glBindTexture(GL_TEXTURE_2D, *out_texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    GLint wrap_s = GL_REPEAT, wrap_t = GL_REPEAT;
    GLint min_filter = GL_LINEAR_MIPMAP_LINEAR, mag_filter = GL_LINEAR;
    if (tex->sampler >= 0 && tex->sampler < doc->sampler_count)
    {
        GLTFSampler *samp = &doc->samplers[tex->sampler];
        wrap_s = samp->wrap_s;
        wrap_t = samp->wrap_t;
        mag_filter = samp->mag_filter;

        if (samp->min_filter == 9984 || samp->min_filter == 9985 ||
            samp->min_filter == 9986 || samp->min_filter == 9987)
            min_filter = samp->min_filter;
        else if (samp->min_filter == 9728 || samp->min_filter == 9729)
            min_filter = samp->min_filter;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
    stbi_image_free(pixels);
    return true;
}

static bool build_mesh_from_primitive(Aroma3DMesh *mesh, const GLTFDocument *doc,
                                      const GLTFPrimitive *prim, const char *base_dir)
{
    if (!mesh || !doc || !prim)
    {
        LOG_ERROR("build_mesh: null arg mesh=%p doc=%p prim=%p", (void *)mesh, (void *)doc, (void *)prim);
        return false;
    }
    LOG_INFO("build_mesh: mode=%d pos_acc=%d norm_acc=%d uv_acc=%d idx_acc=%d",
             prim->mode, prim->pos_accessor, prim->norm_accessor, prim->uv_accessor, prim->indices_accessor);
    if (prim->mode != 4)
    {
        LOG_ERROR("build_mesh: rejected, mode=%d != 4", prim->mode);
        return false;
    }
    memset(mesh, 0, sizeof(Aroma3DMesh));

    int pos_acc = prim->pos_accessor;
    int norm_acc = prim->norm_accessor;
    int uv_acc = prim->uv_accessor;
    int idx_acc = prim->indices_accessor;
    if (pos_acc < 0 || idx_acc < 0 || idx_acc >= doc->accessor_count)
    {
        LOG_ERROR("build_mesh: rejected, pos_acc=%d idx_acc=%d accessor_count=%d",
                  pos_acc, idx_acc, doc->accessor_count);
        return false;
    }

    GLTFBufferView pos_view = gltf_get_buffer_view(doc, (int)doc->accessors[pos_acc].buffer_view);
    GLTFBufferView idx_view = gltf_get_buffer_view(doc, (int)doc->accessors[idx_acc].buffer_view);
    GLTFBufferView norm_view = norm_acc >= 0 ? gltf_get_buffer_view(doc, (int)doc->accessors[norm_acc].buffer_view) : (GLTFBufferView){0};
    GLTFBufferView uv_view = uv_acc >= 0 ? gltf_get_buffer_view(doc, (int)doc->accessors[uv_acc].buffer_view) : (GLTFBufferView){0};

    LOG_INFO("build_mesh: pos_view buffer=%d byte_length=%zu byte_offset=%zu byte_stride=%zu",
             pos_view.buffer, pos_view.byte_length, pos_view.byte_offset, pos_view.byte_stride);

    mesh->vertex_count = (int)doc->accessors[pos_acc].count;
    mesh->index_count = (int)doc->accessors[idx_acc].count;
    mesh->index_type = doc->accessors[idx_acc].component_type;
    mesh->index_offset = doc->accessors[idx_acc].byte_offset;

    glGenVertexArrays(1, &mesh->vao);
    glBindVertexArray(mesh->vao);

    if (!upload_mesh_attr(&mesh->vbo_positions, doc, &pos_view, &doc->accessors[pos_acc], 3))
    {
        LOG_ERROR("build_mesh: upload_mesh_attr failed for positions (buffer_data=%p)",
                  (void *)gltf_buffer_data(doc, (int)pos_view.buffer, pos_view.byte_offset));
        destroy_mesh(mesh);
        return false;
    }
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_positions);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)pos_view.byte_stride, (void *)(uintptr_t)doc->accessors[pos_acc].byte_offset);

    if (norm_view.byte_length > 0 && upload_mesh_attr(&mesh->vbo_normals, doc, &norm_view, &doc->accessors[norm_acc], 3))
    {
        glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_normals);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, (GLsizei)norm_view.byte_stride, (void *)(uintptr_t)doc->accessors[norm_acc].byte_offset);
    }

    if (uv_view.byte_length > 0 && upload_mesh_attr(&mesh->vbo_texcoords, doc, &uv_view, &doc->accessors[uv_acc], 2))
    {
        glEnableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_texcoords);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)uv_view.byte_stride, (void *)(uintptr_t)doc->accessors[uv_acc].byte_offset);
    }

    glGenBuffers(1, &mesh->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    const unsigned char *idx_data = gltf_buffer_data(doc, (int)idx_view.buffer, idx_view.byte_offset);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_view.byte_length, idx_data, GL_STATIC_DRAW);

    glBindVertexArray(0);

    if (prim->material >= 0 && prim->material < doc->material_count)
    {
        GLTFMaterial *mat = &doc->materials[prim->material];
        mesh->has_texture = load_and_upload_texture(doc, mat->base_color_texture,
                                                    &mesh->texture_id, "base_color", base_dir);
        mesh->has_emissive_texture = load_and_upload_texture(doc, mat->emissive_texture,
                                                             &mesh->emissive_texture_id, "emissive", base_dir);
        mesh->base_color[0] = mat->base_color[0];
        mesh->base_color[1] = mat->base_color[1];
        mesh->base_color[2] = mat->base_color[2];
        mesh->alpha = mat->base_color[3];
        mesh->metallic = mat->metallic_factor;

        mesh->clearcoat = AROMA_3D_DEFAULT_CLEARCOAT;
        mesh->alpha_mode = mat->alpha_mode;
        mesh->alpha_cutoff = mat->alpha_cutoff;
        mesh->double_sided = mat->double_sided;
        mesh->emissive_factor[0] = mat->emissive_factor[0];
        mesh->emissive_factor[1] = mat->emissive_factor[1];
        mesh->emissive_factor[2] = mat->emissive_factor[2];
        mesh->emissive_strength = mat->emissive_strength;
    }
    else
    {

        mesh->alpha = 1.0f;
        mesh->alpha_mode = GLTF_ALPHA_OPAQUE;
        mesh->metallic = AROMA_3D_DEFAULT_METALLIC;
        mesh->clearcoat = AROMA_3D_DEFAULT_CLEARCOAT;
    }
    return true;
}

static void mat4_identity16(float m[16])
{
    for (int i = 0; i < 16; i++)
        m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_mul16(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int col = 0; col < 4; col++)
    {
        for (int row = 0; row < 4; row++)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a[k * 4 + row] * b[col * 4 + k];
            r[col * 4 + row] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

static void quat_to_mat4(float m[16], const float q[4])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float len = sqrtf(x * x + y * y + z * z + w * w);
    if (len > 1e-8f)
    {
        x /= len;
        y /= len;
        z /= len;
        w /= len;
    }
    else
    {
        x = y = z = 0.0f;
        w = 1.0f;
    }
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    mat4_identity16(m);
    m[0] = 1.0f - (yy + zz);
    m[1] = xy + wz;
    m[2] = xz - wy;
    m[4] = xy - wz;
    m[5] = 1.0f - (xx + zz);
    m[6] = yz + wx;
    m[8] = xz + wy;
    m[9] = yz - wx;
    m[10] = 1.0f - (xx + yy);
}

static void node_local_transform(const GLTFNode *node, float out[16])
{
    if (node->has_matrix)
    {
        memcpy(out, node->matrix, sizeof(float) * 16);
        return;
    }
    float t[16], r[16], s[16];
    mat4_identity16(t);
    t[12] = node->translation[0];
    t[13] = node->translation[1];
    t[14] = node->translation[2];

    quat_to_mat4(r, node->rotation);

    mat4_identity16(s);
    s[0] = node->scale[0];
    s[5] = node->scale[1];
    s[10] = node->scale[2];

    float tr[16];
    mat4_mul16(tr, t, r);
    mat4_mul16(out, tr, s);
}

static void traverse_node(Aroma3DModel *model, const GLTFDocument *doc, const char *base_dir,
                          int node_index, const float parent_transform[16],
                          int *mesh_idx, bool *hit_cap, bool *visited)
{
    if (node_index < 0 || node_index >= doc->node_count || visited[node_index])
        return;
    visited[node_index] = true;

    const GLTFNode *node = &doc->nodes[node_index];
    float local[16], world[16];
    node_local_transform(node, local);
    mat4_mul16(world, parent_transform, local);

    if (node->mesh >= 0 && node->mesh < doc->mesh_count)
    {
        const GLTFMesh *gltf_mesh = &doc->meshes[node->mesh];
        for (int j = 0; j < gltf_mesh->primitive_count; j++)
        {
            if (*mesh_idx >= AROMA_3D_MAX_MESHES)
            {
                *hit_cap = true;
                return;
            }
            Aroma3DMesh *mesh = &model->meshes[*mesh_idx];
            if (build_mesh_from_primitive(mesh, doc, &gltf_mesh->primitives[j], base_dir))
            {
                memcpy(mesh->world_transform, world, sizeof(float) * 16);

                const GLTFPrimitive *prim = &gltf_mesh->primitives[j];
                if (prim->pos_accessor >= 0 && prim->pos_accessor < doc->accessor_count)
                {
                    const GLTFAccessor *acc = &doc->accessors[prim->pos_accessor];

                    for (int corner = 0; corner < 8; corner++)
                    {
                        float local_pt[3] = {
                            (corner & 1) ? acc->max[0] : acc->min[0],
                            (corner & 2) ? acc->max[1] : acc->min[1],
                            (corner & 4) ? acc->max[2] : acc->min[2],
                        };
                        float wx = world[0] * local_pt[0] + world[4] * local_pt[1] + world[8] * local_pt[2] + world[12];
                        float wy = world[1] * local_pt[0] + world[5] * local_pt[1] + world[9] * local_pt[2] + world[13];
                        float wz = world[2] * local_pt[0] + world[6] * local_pt[1] + world[10] * local_pt[2] + world[14];
                        if (wx < model->bounds_min[0])
                            model->bounds_min[0] = wx;
                        if (wy < model->bounds_min[1])
                            model->bounds_min[1] = wy;
                        if (wz < model->bounds_min[2])
                            model->bounds_min[2] = wz;
                        if (wx > model->bounds_max[0])
                            model->bounds_max[0] = wx;
                        if (wy > model->bounds_max[1])
                            model->bounds_max[1] = wy;
                        if (wz > model->bounds_max[2])
                            model->bounds_max[2] = wz;
                    }
                }
                (*mesh_idx)++;
            }
        }
    }

    for (int c = 0; c < node->child_count && !*hit_cap; c++)
        traverse_node(model, doc, base_dir, node->children[c], world, mesh_idx, hit_cap, visited);
}

static void build_model_from_scene(Aroma3DModel *model, const GLTFDocument *doc, const char *base_dir, int *mesh_idx)
{
    if (doc->node_count <= 0)
        return;

    bool *visited = (bool *)calloc((size_t)doc->node_count, sizeof(bool));
    if (!visited)
        return;
    float identity[16];
    mat4_identity16(identity);
    bool hit_cap = false;

    if (doc->default_scene >= 0 && doc->default_scene < doc->scene_count)
    {
        const GLTFScene *scene = &doc->scenes[doc->default_scene];
        for (int i = 0; i < scene->node_count && !hit_cap; i++)
            traverse_node(model, doc, base_dir, scene->nodes[i], identity, mesh_idx, &hit_cap, visited);
    }
    else
    {

        bool *is_child = (bool *)calloc((size_t)doc->node_count, sizeof(bool));
        if (is_child)
        {
            for (int i = 0; i < doc->node_count; i++)
                for (int c = 0; c < doc->nodes[i].child_count; c++)
                    if (doc->nodes[i].children[c] >= 0 && doc->nodes[i].children[c] < doc->node_count)
                        is_child[doc->nodes[i].children[c]] = true;
            for (int i = 0; i < doc->node_count && !hit_cap; i++)
                if (!is_child[i])
                    traverse_node(model, doc, base_dir, i, identity, mesh_idx, &hit_cap, visited);
            free(is_child);
        }
    }

    free(visited);
    if (hit_cap)
    {
        LOG_ERROR("3D: model instantiates more than AROMA_3D_MAX_MESHES=%d (node, primitive) pairs, "
                  "some parts of the model WILL be missing - raise the cap if this is a legitimate asset",
                  AROMA_3D_MAX_MESHES);
    }
}

static bool stage_mesh_attr(float **out_data, size_t *out_len, const GLTFDocument *doc,
                            const GLTFBufferView *view, const GLTFAccessor *acc, int components)
{
    (void)acc;
    (void)components;
    if (!view || !acc || view->byte_length == 0)
        return false;
    const unsigned char *data = gltf_buffer_data(doc, (int)view->buffer, view->byte_offset);
    if (!data)
        return false;
    float *copy = (float *)malloc(view->byte_length);
    if (!copy)
        return false;
    memcpy(copy, data, view->byte_length);
    *out_data = copy;
    *out_len = view->byte_length;
    return true;
}

static bool stage_texture(const GLTFDocument *doc, int texture_index, const char *base_dir,
                          const char *context_label, unsigned char **out_pixels, int *out_w, int *out_h,
                          GLint *out_wrap_s, GLint *out_wrap_t, GLint *out_min_filter, GLint *out_mag_filter)
{
    if (texture_index < 0 || texture_index >= doc->texture_count)
        return false;

    GLTFTexture *tex = &doc->textures[texture_index];
    if (tex->source_image < 0 || tex->source_image >= doc->image_count)
    {
        LOG_ERROR("build_mesh(async): %s texture[%d].source_image=%d out of range (image_count=%d)",
                  context_label, texture_index, tex->source_image, doc->image_count);
        return false;
    }

    GLTFImage *img = &doc->images[tex->source_image];

    const unsigned char *img_data = NULL;
    size_t img_size = 0;
    unsigned char *uri_owned_data = NULL;

    if (img->buffer_view >= 0 && img->buffer_view < doc->view_count)
    {
        GLTFBufferView *v = &doc->views[img->buffer_view];
        img_data = gltf_buffer_data(doc, v->buffer, v->byte_offset);
        img_size = v->byte_length;
        if (!img_data)
        {
            LOG_ERROR("build_mesh(async): %s gltf_buffer_data returned NULL for image bufferView=%d",
                      context_label, img->buffer_view);
        }
    }

    if (!img_data && img->uri)
    {
        if (!resolve_and_load_uri(img->uri, base_dir, &uri_owned_data, &img_size))
        {
            LOG_ERROR("build_mesh(async): %s image[%d] failed to resolve uri (see prior log line)",
                      context_label, tex->source_image);
            return false;
        }
        img_data = uri_owned_data;
    }

    if (!img_data)
    {
        LOG_ERROR("build_mesh(async): %s image[%d] has neither a valid bufferView (%d, view_count=%d) "
                  "nor a uri - no pixel data available",
                  context_label, tex->source_image, img->buffer_view, doc->view_count);
        return false;
    }

    int w, h, channels;
    unsigned char *pixels = stbi_load_from_memory(img_data, (int)img_size, &w, &h, &channels, 4);
    free(uri_owned_data);
    if (!pixels)
    {
        LOG_ERROR("build_mesh(async): %s stbi_load_from_memory failed to decode image "
                  "(source_image=%d, byte_length=%zu) - corrupt or unsupported format?",
                  context_label, tex->source_image, img_size);
        return false;
    }

    GLint wrap_s = GL_REPEAT, wrap_t = GL_REPEAT;
    GLint min_filter = GL_LINEAR_MIPMAP_LINEAR, mag_filter = GL_LINEAR;
    if (tex->sampler >= 0 && tex->sampler < doc->sampler_count)
    {
        GLTFSampler *samp = &doc->samplers[tex->sampler];
        wrap_s = samp->wrap_s;
        wrap_t = samp->wrap_t;
        mag_filter = samp->mag_filter;

        if (samp->min_filter == 9984 || samp->min_filter == 9985 ||
            samp->min_filter == 9986 || samp->min_filter == 9987)
            min_filter = samp->min_filter;
        else if (samp->min_filter == 9728 || samp->min_filter == 9729)
            min_filter = samp->min_filter;
    }

    *out_pixels = pixels;
    *out_w = w;
    *out_h = h;
    *out_wrap_s = wrap_s;
    *out_wrap_t = wrap_t;
    *out_min_filter = min_filter;
    *out_mag_filter = mag_filter;
    return true;
}

static void free_staged_mesh(StagedMesh *sm)
{
    if (!sm)
        return;
    free(sm->positions);
    free(sm->normals);
    free(sm->texcoords);
    free(sm->indices_raw);
    if (sm->base_color_pixels)
        stbi_image_free(sm->base_color_pixels);
    if (sm->emissive_pixels)
        stbi_image_free(sm->emissive_pixels);
    memset(sm, 0, sizeof(*sm));
}

static bool build_staged_mesh_from_primitive(StagedMesh *sm, const GLTFDocument *doc,
                                             const GLTFPrimitive *prim, const char *base_dir)
{
    if (!sm || !doc || !prim)
        return false;
    if (prim->mode != 4)
    {
        LOG_ERROR("build_mesh(async): rejected, mode=%d != 4", prim->mode);
        return false;
    }
    memset(sm, 0, sizeof(*sm));

    int pos_acc = prim->pos_accessor;
    int norm_acc = prim->norm_accessor;
    int uv_acc = prim->uv_accessor;
    int idx_acc = prim->indices_accessor;
    if (pos_acc < 0 || idx_acc < 0 || idx_acc >= doc->accessor_count)
    {
        LOG_ERROR("build_mesh(async): rejected, pos_acc=%d idx_acc=%d accessor_count=%d",
                  pos_acc, idx_acc, doc->accessor_count);
        return false;
    }

    GLTFBufferView pos_view = gltf_get_buffer_view(doc, (int)doc->accessors[pos_acc].buffer_view);
    GLTFBufferView idx_view = gltf_get_buffer_view(doc, (int)doc->accessors[idx_acc].buffer_view);
    GLTFBufferView norm_view = norm_acc >= 0 ? gltf_get_buffer_view(doc, (int)doc->accessors[norm_acc].buffer_view) : (GLTFBufferView){0};
    GLTFBufferView uv_view = uv_acc >= 0 ? gltf_get_buffer_view(doc, (int)doc->accessors[uv_acc].buffer_view) : (GLTFBufferView){0};

    sm->vertex_count = (int)doc->accessors[pos_acc].count;
    sm->index_count = (int)doc->accessors[idx_acc].count;
    sm->index_type = doc->accessors[idx_acc].component_type;
    sm->index_accessor_offset = doc->accessors[idx_acc].byte_offset;

    if (!stage_mesh_attr(&sm->positions, &sm->pos_len, doc, &pos_view, &doc->accessors[pos_acc], 3))
    {
        LOG_ERROR("build_mesh(async): stage_mesh_attr failed for positions");
        free_staged_mesh(sm);
        return false;
    }
    sm->pos_stride = pos_view.byte_stride;
    sm->pos_accessor_offset = doc->accessors[pos_acc].byte_offset;

    if (norm_view.byte_length > 0 && stage_mesh_attr(&sm->normals, &sm->norm_len, doc, &norm_view, &doc->accessors[norm_acc], 3))
    {
        sm->norm_stride = norm_view.byte_stride;
        sm->norm_accessor_offset = doc->accessors[norm_acc].byte_offset;
    }

    if (uv_view.byte_length > 0 && stage_mesh_attr(&sm->texcoords, &sm->uv_len, doc, &uv_view, &doc->accessors[uv_acc], 2))
    {
        sm->uv_stride = uv_view.byte_stride;
        sm->uv_accessor_offset = doc->accessors[uv_acc].byte_offset;
    }

    const unsigned char *idx_data = gltf_buffer_data(doc, (int)idx_view.buffer, idx_view.byte_offset);
    if (idx_view.byte_length > 0 && idx_data)
    {
        sm->indices_raw = (unsigned char *)malloc(idx_view.byte_length);
        if (sm->indices_raw)
        {
            memcpy(sm->indices_raw, idx_data, idx_view.byte_length);
            sm->indices_raw_size = idx_view.byte_length;
        }
    }
    if (!sm->indices_raw)
    {
        LOG_ERROR("build_mesh(async): failed to stage index buffer");
        free_staged_mesh(sm);
        return false;
    }

    if (prim->material >= 0 && prim->material < doc->material_count)
    {
        GLTFMaterial *mat = &doc->materials[prim->material];
        sm->has_base_color_pixels = stage_texture(doc, mat->base_color_texture, base_dir, "base_color",
                                                  &sm->base_color_pixels, &sm->base_color_w, &sm->base_color_h,
                                                  &sm->base_color_wrap_s, &sm->base_color_wrap_t,
                                                  &sm->base_color_min_filter, &sm->base_color_mag_filter);
        sm->has_emissive_pixels = stage_texture(doc, mat->emissive_texture, base_dir, "emissive",
                                                &sm->emissive_pixels, &sm->emissive_w, &sm->emissive_h,
                                                &sm->emissive_wrap_s, &sm->emissive_wrap_t,
                                                &sm->emissive_min_filter, &sm->emissive_mag_filter);
        sm->base_color[0] = mat->base_color[0];
        sm->base_color[1] = mat->base_color[1];
        sm->base_color[2] = mat->base_color[2];
        sm->alpha = mat->base_color[3];
        sm->metallic = mat->metallic_factor;

        sm->clearcoat = AROMA_3D_DEFAULT_CLEARCOAT;
        sm->alpha_mode = mat->alpha_mode;
        sm->alpha_cutoff = mat->alpha_cutoff;
        sm->double_sided = mat->double_sided;
        sm->emissive_factor[0] = mat->emissive_factor[0];
        sm->emissive_factor[1] = mat->emissive_factor[1];
        sm->emissive_factor[2] = mat->emissive_factor[2];
        sm->emissive_strength = mat->emissive_strength;
    }
    else
    {
        sm->alpha = 1.0f;
        sm->alpha_mode = GLTF_ALPHA_OPAQUE;
        sm->metallic = AROMA_3D_DEFAULT_METALLIC;
        sm->clearcoat = AROMA_3D_DEFAULT_CLEARCOAT;
    }
    return true;
}

static void traverse_node_staged(StagedModel *model, const GLTFDocument *doc, const char *base_dir,
                                 int node_index, const float parent_transform[16],
                                 int *mesh_idx, bool *hit_cap, bool *visited)
{
    if (node_index < 0 || node_index >= doc->node_count || visited[node_index])
        return;
    visited[node_index] = true;

    const GLTFNode *node = &doc->nodes[node_index];
    float local[16], world[16];
    node_local_transform(node, local);
    mat4_mul16(world, parent_transform, local);

    if (node->mesh >= 0 && node->mesh < doc->mesh_count)
    {
        const GLTFMesh *gltf_mesh = &doc->meshes[node->mesh];
        for (int j = 0; j < gltf_mesh->primitive_count; j++)
        {
            if (*mesh_idx >= AROMA_3D_MAX_MESHES)
            {
                *hit_cap = true;
                return;
            }
            StagedMesh *sm = &model->meshes[*mesh_idx];
            if (build_staged_mesh_from_primitive(sm, doc, &gltf_mesh->primitives[j], base_dir))
            {
                memcpy(sm->world_transform, world, sizeof(float) * 16);

                const GLTFPrimitive *prim = &gltf_mesh->primitives[j];
                if (prim->pos_accessor >= 0 && prim->pos_accessor < doc->accessor_count)
                {
                    const GLTFAccessor *acc = &doc->accessors[prim->pos_accessor];

                    for (int corner = 0; corner < 8; corner++)
                    {
                        float local_pt[3] = {
                            (corner & 1) ? acc->max[0] : acc->min[0],
                            (corner & 2) ? acc->max[1] : acc->min[1],
                            (corner & 4) ? acc->max[2] : acc->min[2],
                        };
                        float wx = world[0] * local_pt[0] + world[4] * local_pt[1] + world[8] * local_pt[2] + world[12];
                        float wy = world[1] * local_pt[0] + world[5] * local_pt[1] + world[9] * local_pt[2] + world[13];
                        float wz = world[2] * local_pt[0] + world[6] * local_pt[1] + world[10] * local_pt[2] + world[14];
                        if (wx < model->bounds_min[0])
                            model->bounds_min[0] = wx;
                        if (wy < model->bounds_min[1])
                            model->bounds_min[1] = wy;
                        if (wz < model->bounds_min[2])
                            model->bounds_min[2] = wz;
                        if (wx > model->bounds_max[0])
                            model->bounds_max[0] = wx;
                        if (wy > model->bounds_max[1])
                            model->bounds_max[1] = wy;
                        if (wz > model->bounds_max[2])
                            model->bounds_max[2] = wz;
                    }
                }
                (*mesh_idx)++;
            }
        }
    }

    for (int c = 0; c < node->child_count && !*hit_cap; c++)
        traverse_node_staged(model, doc, base_dir, node->children[c], world, mesh_idx, hit_cap, visited);
}

static void build_staged_model_from_scene(StagedModel *model, const GLTFDocument *doc, const char *base_dir, int *mesh_idx)
{
    if (doc->node_count <= 0)
        return;

    bool *visited = (bool *)calloc((size_t)doc->node_count, sizeof(bool));
    if (!visited)
        return;
    float identity[16];
    mat4_identity16(identity);
    bool hit_cap = false;

    if (doc->default_scene >= 0 && doc->default_scene < doc->scene_count)
    {
        const GLTFScene *scene = &doc->scenes[doc->default_scene];
        for (int i = 0; i < scene->node_count && !hit_cap; i++)
            traverse_node_staged(model, doc, base_dir, scene->nodes[i], identity, mesh_idx, &hit_cap, visited);
    }
    else
    {
        bool *is_child = (bool *)calloc((size_t)doc->node_count, sizeof(bool));
        if (is_child)
        {
            for (int i = 0; i < doc->node_count; i++)
                for (int c = 0; c < doc->nodes[i].child_count; c++)
                    if (doc->nodes[i].children[c] >= 0 && doc->nodes[i].children[c] < doc->node_count)
                        is_child[doc->nodes[i].children[c]] = true;
            for (int i = 0; i < doc->node_count && !hit_cap; i++)
                if (!is_child[i])
                    traverse_node_staged(model, doc, base_dir, i, identity, mesh_idx, &hit_cap, visited);
            free(is_child);
        }
    }

    free(visited);
    if (hit_cap)
    {
        LOG_ERROR("3D: model(async) instantiates more than AROMA_3D_MAX_MESHES=%d (node, primitive) pairs, "
                  "some parts of the model WILL be missing - raise the cap if this is a legitimate asset",
                  AROMA_3D_MAX_MESHES);
    }
}

static char *dirname_alloc(const char *path)
{
    const char *last_slash = strrchr(path, '/');
    if (!last_slash)
        return NULL;
    size_t dir_len = (size_t)(last_slash - path);
    if (dir_len == 0)
        return strdup("/");
    char *dir = (char *)malloc(dir_len + 1);
    if (!dir)
        return NULL;
    memcpy(dir, path, dir_len);
    dir[dir_len] = '\0';
    return dir;
}

static Aroma3DModel *load_model_from_memory_impl(const unsigned char *data, size_t size, const char *base_dir);

Aroma3DModel *aroma_3d_load_model(const char *path)
{
    if (!path)
        return NULL;
    LOG_INFO("3D: attempting to load model: %s", path);
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        LOG_ERROR("3D model not found: %s", path);
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            LOG_ERROR("3D: current working directory: %s", cwd);
        }
        return NULL;
    }
    LOG_INFO("3D: fopen succeeded for %s", path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    LOG_INFO("3D: file size = %ld", size);
    unsigned char *data = NULL;
    if (size > 0)
    {
        data = (unsigned char *)malloc((size_t)size);
        if (data)
            fread(data, 1, (size_t)size, f);
    }
    fclose(f);

    char *base_dir = dirname_alloc(path);
    Aroma3DModel *model = load_model_from_memory_impl(data, (size_t)size, base_dir);
    free(base_dir);
    free(data);
    if (!model)
        LOG_ERROR("3D: load_model_from_memory failed for %s", path);
    return model;
}

Aroma3DModel *aroma_3d_load_model_from_memory(const unsigned char *data, size_t size)
{

    return load_model_from_memory_impl(data, size, NULL);
}

static GLTFDocument *parse_gltf_document_from_bytes(const unsigned char *data, size_t size, const char *base_dir)
{
    if (!data || size < 12)
        return NULL;
    const unsigned char *json_text = NULL;
    size_t json_size = 0;
    const unsigned char *bin_data = NULL;
    size_t bin_size = 0;

    uint32_t magic = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    LOG_INFO("3D: GLB magic=0x%08X size=%zu", magic, size);
    if (magic == 0x46546C67)
    {
        uint32_t total = (uint32_t)data[8] | ((uint32_t)data[9] << 8) | ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
        size_t offset = 12;
        LOG_INFO("3D: GLB total=%u, size=%zu", total, size);
        while (offset + 8 <= (size_t)size && offset + 8 <= total)
        {
            uint32_t chunk_len = (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
            uint32_t chunk_type = (uint32_t)data[offset + 4] | ((uint32_t)data[offset + 5] << 8) | ((uint32_t)data[offset + 6] << 16) | ((uint32_t)data[offset + 7] << 24);
            LOG_INFO("3D: chunk at offset=%zu len=%u type=0x%08X", offset, chunk_len, chunk_type);
            if (offset + 8 + chunk_len > total || offset + 8 + chunk_len > (size_t)size)
                break;
            if (chunk_type == 0x4E4F534A)
            {
                json_text = data + offset + 8;
                json_size = chunk_len;
                LOG_INFO("3D: found JSON chunk, size=%zu", json_size);
            }
            else if (chunk_type == 0x004E4942)
            {
                bin_data = data + offset + 8;
                bin_size = chunk_len;
                LOG_INFO("3D: found BIN chunk, size=%zu", bin_size);
            }
            offset += 8 + chunk_len;
        }
    }
    else
    {
        json_text = data;
        json_size = size;
    }

    if (!json_text || json_size == 0)
    {
        LOG_ERROR("3D: no JSON chunk found");
        return NULL;
    }
    LOG_INFO("3D: json_size=%zu bin_size=%zu", json_size, bin_size);

    GLTFDocument *doc = NULL;
    cJSON *root = cJSON_Parse((const char *)json_text);
    if (!root)
    {
        LOG_ERROR("3D: Failed to parse GLTF JSON");
        return NULL;
    }

    if (!cJSON_IsObject(root))
    {
        LOG_ERROR("3D: root is not an object");
        cJSON_Delete(root);
        return NULL;
    }
    if (!gltf_parse_document_from_root(root, &doc))
    {
        LOG_ERROR("3D: gltf_parse_document_from_root failed");
        cJSON_Delete(root);
        return NULL;
    }
    LOG_INFO("3D: glTF parsed, meshes=%d, materials=%d, nodes=%d, scenes=%d",
             doc->mesh_count, doc->material_count, doc->node_count, doc->scene_count);
    cJSON_Delete(root);
    gltf_load_buffers(doc, bin_data, bin_size, base_dir);
    return doc;
}

static Aroma3DModel *load_model_from_memory_impl(const unsigned char *data, size_t size, const char *base_dir)
{
    GLTFDocument *doc = parse_gltf_document_from_bytes(data, size, base_dir);
    if (!doc)
        return NULL;

    Aroma3DModel *model = (Aroma3DModel *)calloc(1, sizeof(Aroma3DModel));
    if (!model)
    {
        gltf_document_free(doc);
        return NULL;
    }
    for (int k = 0; k < 3; k++)
    {
        model->bounds_min[k] = INFINITY;
        model->bounds_max[k] = -INFINITY;
    }

    int mesh_idx = 0;
    build_model_from_scene(model, doc, base_dir, &mesh_idx);
    model->mesh_count = mesh_idx;

    for (int k = 0; k < 3; k++)
    {
        if (!isfinite(model->bounds_min[k]) || !isfinite(model->bounds_max[k]))
        {
            model->bounds_min[k] = 0.0f;
            model->bounds_max[k] = 0.0f;
        }
    }

    LOG_INFO("3D: model bounds min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f)",
             model->bounds_min[0], model->bounds_min[1], model->bounds_min[2],
             model->bounds_max[0], model->bounds_max[1], model->bounds_max[2]);

    gltf_document_free(doc);
    LOG_INFO("Loaded 3D model with %d meshes", model->mesh_count);
    return model;
}

static void *aroma_3d_load_model_worker_fn(void *arg)
{
    struct Aroma3DLoadJob *job = (struct Aroma3DLoadJob *)arg;

    StagedModel *staged = (StagedModel *)calloc(1, sizeof(StagedModel));
    bool ok = false;

    if (staged)
    {
        for (int k = 0; k < 3; k++)
        {
            staged->bounds_min[k] = INFINITY;
            staged->bounds_max[k] = -INFINITY;
        }

        GLTFDocument *doc = parse_gltf_document_from_bytes(job->input_data, job->input_size, job->input_base_dir);
        if (doc)
        {
            int mesh_idx = 0;
            build_staged_model_from_scene(staged, doc, job->input_base_dir, &mesh_idx);
            staged->mesh_count = mesh_idx;

            for (int k = 0; k < 3; k++)
            {
                if (!isfinite(staged->bounds_min[k]) || !isfinite(staged->bounds_max[k]))
                {
                    staged->bounds_min[k] = 0.0f;
                    staged->bounds_max[k] = 0.0f;
                }
            }

            gltf_document_free(doc);
            ok = true;
            LOG_INFO("3D(async): staged %d meshes off-thread", staged->mesh_count);
        }
        else
        {
            LOG_ERROR("3D(async): parse_gltf_document_from_bytes failed");
        }
    }

    job->staged = staged;
    job->parse_ok = ok;

    atomic_store(&job->status, ok ? AROMA_3D_LOAD_JOB_SUCCESS : AROMA_3D_LOAD_JOB_FAILED);
    return NULL;
}

Aroma3DLoadJob *aroma_3d_load_model_async(const char *path)
{
    if (!path)
        return NULL;
    LOG_INFO("3D(async): queuing load for %s", path);

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        LOG_ERROR("3D model not found: %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0)
    {
        LOG_ERROR("3D(async): %s is empty or unreadable", path);
        fclose(f);
        return NULL;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (!data)
    {
        fclose(f);
        return NULL;
    }
    size_t read_bytes = fread(data, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size)
    {
        LOG_ERROR("3D(async): short read on %s (%zu/%ld bytes)", path, read_bytes, size);
        free(data);
        return NULL;
    }

    struct Aroma3DLoadJob *job = (struct Aroma3DLoadJob *)calloc(1, sizeof(struct Aroma3DLoadJob));
    if (!job)
    {
        free(data);
        return NULL;
    }
    job->input_data = data;
    job->input_size = read_bytes;
    job->input_base_dir = dirname_alloc(path);
    atomic_init(&job->status, AROMA_3D_LOAD_JOB_RUNNING);

    if (pthread_create(&job->thread, NULL, aroma_3d_load_model_worker_fn, job) != 0)
    {
        LOG_ERROR("3D(async): pthread_create failed for %s", path);
        free(job->input_data);
        free(job->input_base_dir);
        free(job);
        return NULL;
    }
    job->thread_started = true;
    return (Aroma3DLoadJob *)job;
}

Aroma3DLoadJob *aroma_3d_load_model_from_memory_async(const unsigned char *data, size_t size)
{
    if (!data || size == 0)
        return NULL;

    unsigned char *data_copy = (unsigned char *)malloc(size);
    if (!data_copy)
        return NULL;
    memcpy(data_copy, data, size);

    struct Aroma3DLoadJob *job = (struct Aroma3DLoadJob *)calloc(1, sizeof(struct Aroma3DLoadJob));
    if (!job)
    {
        free(data_copy);
        return NULL;
    }
    job->input_data = data_copy;
    job->input_size = size;
    job->input_base_dir = NULL;
    atomic_init(&job->status, AROMA_3D_LOAD_JOB_RUNNING);

    if (pthread_create(&job->thread, NULL, aroma_3d_load_model_worker_fn, job) != 0)
    {
        LOG_ERROR("3D(async): pthread_create failed for in-memory load");
        free(job->input_data);
        free(job);
        return NULL;
    }
    job->thread_started = true;
    return (Aroma3DLoadJob *)job;
}

bool aroma_3d_load_model_poll(Aroma3DLoadJob *job)
{
    if (!job)
        return false;
    int status = atomic_load(&((struct Aroma3DLoadJob *)job)->status);
    return status == AROMA_3D_LOAD_JOB_SUCCESS || status == AROMA_3D_LOAD_JOB_FAILED;
}

static void free_staged_model(StagedModel *staged)
{
    if (!staged)
        return;
    for (int i = 0; i < staged->mesh_count; i++)
        free_staged_mesh(&staged->meshes[i]);
    free(staged);
}

static void free_load_job(struct Aroma3DLoadJob *job)
{
    if (!job)
        return;
    free(job->input_data);
    free(job->input_base_dir);
    free(job);
}

Aroma3DModel *aroma_3d_load_model_finish(Aroma3DLoadJob *job_opaque)
{
    struct Aroma3DLoadJob *job = (struct Aroma3DLoadJob *)job_opaque;
    if (!job)
        return NULL;

    if (!aroma_3d_load_model_poll(job_opaque))
    {
        LOG_ERROR("3D(async): aroma_3d_load_model_finish called before the job finished - "
                  "call aroma_3d_load_model_poll() first and only finish once it returns true");
        return NULL;
    }

    if (job->thread_started)
        pthread_join(job->thread, NULL);

    if (atomic_load(&job->status) != AROMA_3D_LOAD_JOB_SUCCESS || !job->staged)
    {
        LOG_ERROR("3D(async): background parse failed, no model produced");
        free_staged_model(job->staged);
        free_load_job(job);
        return NULL;
    }

    StagedModel *staged = job->staged;
    Aroma3DModel *model = (Aroma3DModel *)calloc(1, sizeof(Aroma3DModel));
    if (!model)
    {
        free_staged_model(staged);
        free_load_job(job);
        return NULL;
    }

    int out_idx = 0;
    for (int i = 0; i < staged->mesh_count; i++)
    {
        StagedMesh *sm = &staged->meshes[i];
        Aroma3DMesh *mesh = &model->meshes[out_idx];
        memset(mesh, 0, sizeof(*mesh));

        mesh->vertex_count = sm->vertex_count;
        mesh->index_count = sm->index_count;
        mesh->index_type = sm->index_type;
        mesh->index_offset = sm->index_accessor_offset;

        glGenVertexArrays(1, &mesh->vao);
        glBindVertexArray(mesh->vao);

        if (!sm->positions)
        {
            LOG_ERROR("3D(async): finish: mesh[%d] has no staged position data, skipping", i);
            glBindVertexArray(0);
            destroy_mesh(mesh);
            continue;
        }

        glGenBuffers(1, &mesh->vbo_positions);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_positions);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sm->pos_len, sm->positions, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sm->pos_stride, (void *)(uintptr_t)sm->pos_accessor_offset);

        if (sm->normals)
        {
            glGenBuffers(1, &mesh->vbo_normals);
            glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_normals);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sm->norm_len, sm->normals, GL_STATIC_DRAW);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, (GLsizei)sm->norm_stride, (void *)(uintptr_t)sm->norm_accessor_offset);
        }

        if (sm->texcoords)
        {
            glGenBuffers(1, &mesh->vbo_texcoords);
            glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_texcoords);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sm->uv_len, sm->texcoords, GL_STATIC_DRAW);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)sm->uv_stride, (void *)(uintptr_t)sm->uv_accessor_offset);
        }

        glGenBuffers(1, &mesh->ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sm->indices_raw_size, sm->indices_raw, GL_STATIC_DRAW);

        glBindVertexArray(0);

        if (sm->has_base_color_pixels)
        {
            glGenTextures(1, &mesh->texture_id);
            glBindTexture(GL_TEXTURE_2D, mesh->texture_id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sm->base_color_w, sm->base_color_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, sm->base_color_pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sm->base_color_wrap_s);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sm->base_color_wrap_t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sm->base_color_min_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sm->base_color_mag_filter);
            mesh->has_texture = true;
        }

        if (sm->has_emissive_pixels)
        {
            glGenTextures(1, &mesh->emissive_texture_id);
            glBindTexture(GL_TEXTURE_2D, mesh->emissive_texture_id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sm->emissive_w, sm->emissive_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, sm->emissive_pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sm->emissive_wrap_s);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sm->emissive_wrap_t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sm->emissive_min_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sm->emissive_mag_filter);
            mesh->has_emissive_texture = true;
        }

        mesh->base_color[0] = sm->base_color[0];
        mesh->base_color[1] = sm->base_color[1];
        mesh->base_color[2] = sm->base_color[2];
        mesh->alpha = sm->alpha;
        mesh->metallic = sm->metallic;
        mesh->clearcoat = sm->clearcoat;
        mesh->alpha_mode = sm->alpha_mode;
        mesh->alpha_cutoff = sm->alpha_cutoff;
        mesh->double_sided = sm->double_sided;
        mesh->emissive_factor[0] = sm->emissive_factor[0];
        mesh->emissive_factor[1] = sm->emissive_factor[1];
        mesh->emissive_factor[2] = sm->emissive_factor[2];
        mesh->emissive_strength = sm->emissive_strength;
        memcpy(mesh->world_transform, sm->world_transform, sizeof(float) * 16);

        out_idx++;
    }
    model->mesh_count = out_idx;
    memcpy(model->bounds_min, staged->bounds_min, sizeof(model->bounds_min));
    memcpy(model->bounds_max, staged->bounds_max, sizeof(model->bounds_max));

    LOG_INFO("3D(async): finish uploaded %d meshes to GL", model->mesh_count);

    free_staged_model(staged);
    atomic_store(&job->status, AROMA_3D_LOAD_JOB_CONSUMED);
    free_load_job(job);
    return model;
}

void aroma_3d_load_model_cancel(Aroma3DLoadJob *job_opaque)
{
    struct Aroma3DLoadJob *job = (struct Aroma3DLoadJob *)job_opaque;
    if (!job)
        return;

    if (job->thread_started)
        pthread_join(job->thread, NULL);

    free_staged_model(job->staged);
    free_load_job(job);
}

void aroma_3d_destroy_model(Aroma3DModel *model)
{
    if (!model)
        return;
    for (int i = 0; i < model->mesh_count; i++)
        destroy_mesh(&model->meshes[i]);
    free(model);
}

int aroma_3d_get_mesh_count(const Aroma3DModel *model)
{
    return model ? model->mesh_count : 0;
}

const Aroma3DMesh *aroma_3d_get_mesh(const Aroma3DModel *model, int index)
{
    if (!model || index < 0 || index >= model->mesh_count)
        return NULL;
    return &model->meshes[index];
}

bool aroma_3d_mesh_is_alpha_blended(const Aroma3DMesh *mesh)
{
    if (!mesh)
        return false;
    return mesh->alpha_mode == GLTF_ALPHA_BLEND;
}

void aroma_3d_set_mesh_metallic(Aroma3DModel *model, int index, float metallic)
{
    if (!model || index < 0 || index >= model->mesh_count)
        return;
    if (metallic < 0.0f)
        metallic = 0.0f;
    else if (metallic > 1.0f)
        metallic = 1.0f;
    model->meshes[index].metallic = metallic;
}

void aroma_3d_set_mesh_clearcoat(Aroma3DModel *model, int index, float clearcoat)
{
    if (!model || index < 0 || index >= model->mesh_count)
        return;
    if (clearcoat < 0.0f)
        clearcoat = 0.0f;
    else if (clearcoat > 1.0f)
        clearcoat = 1.0f;
    model->meshes[index].clearcoat = clearcoat;
}

void aroma_3d_get_model_bounds(const Aroma3DModel *model, float out_min[3], float out_max[3])
{
    if (!model)
    {
        if (out_min)
        {
            out_min[0] = out_min[1] = out_min[2] = 0.0f;
        }
        if (out_max)
        {
            out_max[0] = out_max[1] = out_max[2] = 0.0f;
        }
        return;
    }
    if (out_min)
    {
        out_min[0] = model->bounds_min[0];
        out_min[1] = model->bounds_min[1];
        out_min[2] = model->bounds_min[2];
    }
    if (out_max)
    {
        out_max[0] = model->bounds_max[0];
        out_max[1] = model->bounds_max[1];
        out_max[2] = model->bounds_max[2];
    }
}

void aroma_3d_camera_init(Aroma3DCamera *camera)
{
    if (!camera)
        return;
    camera->theta = 0.0f;
    camera->phi = 1.0f;
    camera->radius = 5.0f;
    camera->target[0] = 0.0f;
    camera->target[1] = 0.0f;
    camera->target[2] = 0.0f;
    camera->fov = 45.0f;
    camera->near_plane = 0.1f;
    camera->far_plane = 1000.0f;
}

void aroma_3d_camera_orbit(Aroma3DCamera *camera, float dx, float dy)
{
    if (!camera)
        return;
    camera->theta -= dx * 0.01f;
    camera->phi -= dy * 0.01f;
    if (camera->phi < 0.1f)
        camera->phi = 0.1f;
    if (camera->phi > 3.1f)
        camera->phi = 3.1f;
}

void aroma_3d_camera_zoom(Aroma3DCamera *camera, float delta)
{
    if (!camera)
        return;

    float abs_delta = fabsf(delta);
    if (abs_delta > 1.0f)
        delta = copysignf(1.0f, delta);

    float zoom_factor = 1.0f - delta * 0.15f;
    if (zoom_factor > 1.25f)
        zoom_factor = 1.25f;
    if (zoom_factor < 0.75f)
        zoom_factor = 0.75f;
    camera->radius *= zoom_factor;

    if (camera->radius < 0.1f)
        camera->radius = 0.1f;
    if (camera->radius > 1000.0f)
        camera->radius = 1000.0f;
}

static void camera_eye_position(const Aroma3DCamera *camera, vec3 out_eye)
{
    float sin_theta = sinf(camera->theta);
    float cos_theta = cosf(camera->theta);
    float sin_phi = sinf(camera->phi);
    float cos_phi = cosf(camera->phi);
    out_eye[0] = camera->target[0] + camera->radius * sin_phi * sin_theta;
    out_eye[1] = camera->target[1] + camera->radius * cos_phi;
    out_eye[2] = camera->target[2] + camera->radius * sin_phi * cos_theta;
}

void aroma_3d_camera_pan(Aroma3DCamera *camera, float dx, float dy)
{
    if (!camera)
        return;
    vec3 eye, right;
    float sin_theta = sinf(camera->theta);
    float cos_theta = cosf(camera->theta);
    float sin_phi = sinf(camera->phi);
    float cos_phi = cosf(camera->phi);
    eye[0] = camera->target[0] + camera->radius * sin_phi * sin_theta;
    eye[1] = camera->target[1] + camera->radius * cos_phi;
    eye[2] = camera->target[2] + camera->radius * sin_phi * cos_theta;
    vec3 forward;
    vec3_sub(forward, camera->target, eye);
    vec3_norm(forward, forward);
    right[0] = forward[2];
    right[1] = 0.0f;
    right[2] = -forward[0];
    vec3_norm(right, right);
    vec3 up = {0.0f, 1.0f, 0.0f};
    vec3_scale(right, right, dx * 0.01f);
    vec3_scale(up, up, dy * 0.01f);
    vec3_add(camera->target, camera->target, right);
    vec3_add(camera->target, camera->target, up);
}

void aroma_3d_camera_update_view(const Aroma3DCamera *camera, float view[16])
{
    if (!camera)
        return;
    mat4x4 tmp;
    vec3 eye;
    float sin_theta = sinf(camera->theta);
    float cos_theta = cosf(camera->theta);
    float sin_phi = sinf(camera->phi);
    float cos_phi = cosf(camera->phi);
    eye[0] = camera->target[0] + camera->radius * sin_phi * sin_theta;
    eye[1] = camera->target[1] + camera->radius * cos_phi;
    eye[2] = camera->target[2] + camera->radius * sin_phi * cos_theta;
    vec3 up = {0.0f, 1.0f, 0.0f};
    mat4x4_look_at(tmp, eye, camera->target, up);
    memcpy(view, tmp, 16 * sizeof(float));
}

void aroma_3d_camera_update_proj(const Aroma3DCamera *camera, float proj[16], float aspect)
{
    if (!camera)
        return;
    mat4x4 tmp;
    mat4x4_perspective(tmp, camera->fov * 3.1415926535f / 180.0f, aspect, camera->near_plane, camera->far_plane);
    memcpy(proj, tmp, 16 * sizeof(float));
}

bool aroma_3d_init(void)
{
    return init_3d_resources();
}

void aroma_3d_shutdown(void)
{
    if (g_3d_shader_program)
    {
        glDeleteProgram(g_3d_shader_program);
        g_3d_shader_program = 0;
    }
    g_3d_initialized = false;
}

void aroma_3d_set_light_position(float x, float y, float z)
{
    g_3d_light_position[0] = x;
    g_3d_light_position[1] = y;
    g_3d_light_position[2] = z;
}

static void bind_mesh_model_matrix(const Aroma3DMesh *mesh)
{
    glUniformMatrix4fv(g_u_model, 1, GL_FALSE, mesh->world_transform);

    const float *m = mesh->world_transform;
    float a00 = m[0], a01 = m[4], a02 = m[8];
    float a10 = m[1], a11 = m[5], a12 = m[9];
    float a20 = m[2], a21 = m[6], a22 = m[10];

    float c00 = a11 * a22 - a12 * a21;
    float c01 = a12 * a20 - a10 * a22;
    float c02 = a10 * a21 - a11 * a20;
    float det = a00 * c00 + a01 * c01 + a02 * c02;

    float normal_mat[9];
    if (fabsf(det) > 1e-8f)
    {
        float inv_det = 1.0f / det;

        normal_mat[0] = c00 * inv_det;
        normal_mat[1] = c01 * inv_det;
        normal_mat[2] = c02 * inv_det;
        normal_mat[3] = (a02 * a21 - a01 * a22) * inv_det;
        normal_mat[4] = (a00 * a22 - a02 * a20) * inv_det;
        normal_mat[5] = (a01 * a20 - a00 * a21) * inv_det;
        normal_mat[6] = (a01 * a12 - a02 * a11) * inv_det;
        normal_mat[7] = (a02 * a10 - a00 * a12) * inv_det;
        normal_mat[8] = (a00 * a11 - a01 * a10) * inv_det;
    }
    else
    {

        normal_mat[0] = normal_mat[4] = normal_mat[8] = 1.0f;
        normal_mat[1] = normal_mat[2] = normal_mat[3] = 0.0f;
        normal_mat[5] = normal_mat[6] = normal_mat[7] = 0.0f;
    }
    glUniformMatrix3fv(g_u_normal_matrix, 1, GL_FALSE, normal_mat);
}

static void bind_mesh_uniforms(const Aroma3DMesh *mesh)
{
    glUniform3f(g_u_base_color, mesh->base_color[0], mesh->base_color[1], mesh->base_color[2]);
    glUniform1f(g_u_alpha, mesh->alpha);
    glUniform1i(g_u_alpha_mode, (int)mesh->alpha_mode);
    glUniform1f(g_u_alpha_cutoff, mesh->alpha_cutoff);

    if (mesh->has_texture && mesh->texture_id != 0)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mesh->texture_id);
        glUniform1i(g_u_base_tex, 0);
    }
    glUniform1i(g_u_has_texture, mesh->has_texture ? 1 : 0);

    glUniform3f(g_u_emissive_factor, mesh->emissive_factor[0], mesh->emissive_factor[1], mesh->emissive_factor[2]);
    glUniform1f(g_u_emissive_strength, mesh->emissive_strength);
    if (mesh->has_emissive_texture && mesh->emissive_texture_id != 0)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mesh->emissive_texture_id);
        glUniform1i(g_u_emissive_tex, 1);
    }
    glUniform1i(g_u_has_emissive_texture, mesh->has_emissive_texture ? 1 : 0);

    glUniform1f(g_u_metallic, mesh->metallic);
    glUniform1f(g_u_clearcoat, mesh->clearcoat);
}

bool aroma_3d_render_frame(const Aroma3DModel *model, const Aroma3DCamera *camera, float aspect)
{
    if (!model || !camera || !g_3d_initialized)
        return false;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_DEPTH_BUFFER_BIT);

    float view[16];
    float proj[16];
    aroma_3d_camera_update_view(camera, view);
    aroma_3d_camera_update_proj(camera, proj, aspect);
    LOG_INFO("3D: rendering frame with camera theta=%.2f phi=%.2f radius=%.2f target=(%.2f,%.2f,%.2f) fov=%.2f near=%.2f far=%.2f aspect=%.2f",
             camera->theta, camera->phi, camera->radius,
             camera->target[0], camera->target[1], camera->target[2],
             camera->fov, camera->near_plane, camera->far_plane, aspect);

    glUseProgram(g_3d_shader_program);
    glUniformMatrix4fv(g_u_view, 1, GL_FALSE, &view[0]);
    glUniformMatrix4fv(g_u_proj, 1, GL_FALSE, &proj[0]);

    vec3 eye_pos;
    camera_eye_position(camera, eye_pos);
    glUniform3f(g_u_light_pos, g_3d_light_position[0], g_3d_light_position[1], g_3d_light_position[2]);
    glUniform3f(g_u_eye_pos, eye_pos[0], eye_pos[1], eye_pos[2]);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    for (int i = 0; i < model->mesh_count; i++)
    {
        const Aroma3DMesh *mesh = &model->meshes[i];
        if (!mesh->vao || mesh->alpha_mode == GLTF_ALPHA_BLEND)
            continue;

        glDisable(GL_CULL_FACE);

        bind_mesh_model_matrix(mesh);
        bind_mesh_uniforms(mesh);

        glBindVertexArray(mesh->vao);
        glDrawElements(GL_TRIANGLES, mesh->index_count, mesh->index_type, (void *)(uintptr_t)mesh->index_offset);
        glBindVertexArray(0);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (int i = 0; i < model->mesh_count; i++)
    {
        const Aroma3DMesh *mesh = &model->meshes[i];
        if (!mesh->vao || mesh->alpha_mode != GLTF_ALPHA_BLEND)
            continue;

        glDisable(GL_CULL_FACE);

        bind_mesh_model_matrix(mesh);
        bind_mesh_uniforms(mesh);

        glBindVertexArray(mesh->vao);
        glDrawElements(GL_TRIANGLES, mesh->index_count, mesh->index_type, (void *)(uintptr_t)mesh->index_offset);
        glBindVertexArray(0);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    return true;
}

Aroma3DModel *aroma_3d_create_cube(void)
{
    Aroma3DModel *model = (Aroma3DModel *)calloc(1, sizeof(Aroma3DModel));
    if (!model)
        return NULL;

    float positions[] = {
        -0.5f,
        -0.5f,
        -0.5f,
        0.5f,
        -0.5f,
        -0.5f,
        0.5f,
        0.5f,
        -0.5f,
        -0.5f,
        0.5f,
        -0.5f,
        -0.5f,
        -0.5f,
        0.5f,
        0.5f,
        -0.5f,
        0.5f,
        0.5f,
        0.5f,
        0.5f,
        -0.5f,
        0.5f,
        0.5f,
        -0.5f,
        0.5f,
        0.5f,
        -0.5f,
        0.5f,
        -0.5f,
        -0.5f,
        -0.5f,
        -0.5f,
        -0.5f,
        -0.5f,
        0.5f,
        0.5f,
        0.5f,
        0.5f,
        0.5f,
        0.5f,
        -0.5f,
        0.5f,
        -0.5f,
        -0.5f,
        0.5f,
        -0.5f,
        0.5f,
        -0.5f,
        -0.5f,
        -0.5f,
        -0.5f,
        0.5f,
        -0.5f,
        0.5f,
        0.5f,
        -0.5f,
        0.5f,
        -0.5f,
        -0.5f,
        0.5f,
        -0.5f,
        0.5f,
        0.5f,
        0.5f,
        0.5f,
        -0.5f,
        0.5f,
        0.5f,
        -0.5f,
        -0.5f,
        0.5f,
    };
    float normals[] = {
        0,
        0,
        -1,
        0,
        0,
        -1,
        0,
        0,
        -1,
        0,
        0,
        -1,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
        0,
        1,
        -1,
        0,
        0,
        -1,
        0,
        0,
        -1,
        0,
        0,
        -1,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
        0,
        0,
        -1,
        0,
        0,
        -1,
        0,
        0,
        -1,
        0,
        0,
        -1,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
    };
    unsigned int indices[] = {
        0,
        1,
        2,
        0,
        2,
        3,
        4,
        5,
        6,
        4,
        6,
        7,
        8,
        9,
        10,
        8,
        10,
        11,
        12,
        13,
        14,
        12,
        14,
        15,
        16,
        17,
        18,
        16,
        18,
        19,
        20,
        21,
        22,
        20,
        22,
        23,
    };

    Aroma3DMesh *mesh = &model->meshes[0];
    mesh->vertex_count = 24;
    mesh->index_count = 36;
    mesh->index_type = GL_UNSIGNED_INT;
    mesh->base_color[0] = 0.2f;
    mesh->base_color[1] = 0.6f;
    mesh->base_color[2] = 0.9f;

    mesh->alpha = 1.0f;
    mesh->alpha_mode = GLTF_ALPHA_OPAQUE;
    mesh->metallic = AROMA_3D_DEFAULT_METALLIC;
    mesh->clearcoat = AROMA_3D_DEFAULT_CLEARCOAT;

    glGenVertexArrays(1, &mesh->vao);
    glBindVertexArray(mesh->vao);

    glGenBuffers(1, &mesh->vbo_positions);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_positions);
    glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

    glGenBuffers(1, &mesh->vbo_normals);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_normals);
    glBufferData(GL_ARRAY_BUFFER, sizeof(normals), normals, GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);

    glGenBuffers(1, &mesh->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glBindVertexArray(0);
    model->mesh_count = 1;
    model->bounds_min[0] = model->bounds_min[1] = model->bounds_min[2] = -0.5f;
    model->bounds_max[0] = model->bounds_max[1] = model->bounds_max[2] = 0.5f;
    return model;
}

bool aroma_3d_render_to_rect(const Aroma3DModel *model, const Aroma3DCamera *camera, int x, int y, int w, int h, int win_w, int win_h)
{
    if (!model || !camera || !g_3d_initialized || w <= 0 || h <= 0)
    {
        LOG_ERROR("aroma_3d_render_to_rect failed checks: model=%p, camera=%p, init=%d, w=%d, h=%d", model, camera, g_3d_initialized, w, h);
        return false;
    }

    GLint prev_program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    GLint prev_vao;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);
    GLint prev_scissor_box[4];
    glGetIntegerv(GL_SCISSOR_BOX, prev_scissor_box);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);

    int effective_win_h = win_h > 0 ? win_h : (y + h);
    int gl_y = effective_win_h - y - h;

    GLboolean prev_depth_mask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);

    glEnable(GL_SCISSOR_TEST);
    glScissor(x, gl_y, w, h);
    glViewport(x, gl_y, w, h);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);

    float aspect = (float)w / (float)h;
    aroma_3d_render_frame(model, camera, aspect);

    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(prev_depth_mask);

    glUseProgram(prev_program);
    glBindVertexArray(prev_vao);
    if (prev_depth)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    if (prev_scissor)
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(prev_scissor_box[0], prev_scissor_box[1], prev_scissor_box[2], prev_scissor_box[3]);
    }
    else
    {
        glDisable(GL_SCISSOR_TEST);
    }
    if (prev_blend)
        glEnable(GL_BLEND);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);

    return true;
}
#endif