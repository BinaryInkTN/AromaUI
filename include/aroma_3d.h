#ifndef AROMA_3D_H
#define AROMA_3D_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct Aroma3DModel Aroma3DModel;
    typedef struct Aroma3DMesh Aroma3DMesh;

    typedef struct
    {
        float theta;
        float phi;
        float radius;
        float target[3];
        float fov;
        float near_plane;
        float far_plane;
    } Aroma3DCamera;

    Aroma3DModel *aroma_3d_load_model(const char *path);
    Aroma3DModel *aroma_3d_load_model_from_memory(const unsigned char *data, size_t size);
    Aroma3DModel *aroma_3d_create_cube(void);
    void aroma_3d_destroy_model(Aroma3DModel *model);
    int aroma_3d_get_mesh_count(const Aroma3DModel *model);
    const Aroma3DMesh *aroma_3d_get_mesh(const Aroma3DModel *model, int index);

    bool aroma_3d_mesh_is_alpha_blended(const Aroma3DMesh *mesh);
    void aroma_3d_get_model_bounds(const Aroma3DModel *model, float out_min[3], float out_max[3]);

    void aroma_3d_set_mesh_metallic(Aroma3DModel *model, int index, float metallic);

    void aroma_3d_set_mesh_clearcoat(Aroma3DModel *model, int index, float clearcoat);

    void aroma_3d_camera_init(Aroma3DCamera *camera);
    void aroma_3d_camera_orbit(Aroma3DCamera *camera, float dx, float dy);
    void aroma_3d_camera_zoom(Aroma3DCamera *camera, float delta);
    void aroma_3d_camera_pan(Aroma3DCamera *camera, float dx, float dy);
    void aroma_3d_camera_update_view(const Aroma3DCamera *camera, float view[16]);
    void aroma_3d_camera_update_proj(const Aroma3DCamera *camera, float proj[16], float aspect);
    void aroma_3d_set_light_position(float x, float y, float z);
    bool aroma_3d_init(void);
    void aroma_3d_shutdown(void);
    bool aroma_3d_render_frame(const Aroma3DModel *model, const Aroma3DCamera *camera, float aspect);
    bool aroma_3d_render_to_rect(const Aroma3DModel *model, const Aroma3DCamera *camera, int x, int y, int w, int h, int win_w, int win_h);

    typedef struct Aroma3DLoadJob Aroma3DLoadJob;

    Aroma3DLoadJob *aroma_3d_load_model_async(const char *path);

    Aroma3DLoadJob *aroma_3d_load_model_from_memory_async(const unsigned char *data, size_t size);

    bool aroma_3d_load_model_poll(Aroma3DLoadJob *job);

    Aroma3DModel *aroma_3d_load_model_finish(Aroma3DLoadJob *job);

    void aroma_3d_load_model_cancel(Aroma3DLoadJob *job);
#ifdef __cplusplus
}
#endif

#endif