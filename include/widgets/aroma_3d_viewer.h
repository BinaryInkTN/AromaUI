#ifndef AROMA_3D_VIEWER_H
#define AROMA_3D_VIEWER_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_3d.h"

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct Aroma3DViewer Aroma3DViewer;

    AromaNode *aroma_3d_viewer_create(AromaNode *parent, int x, int y, int width, int height);
    void aroma_3d_viewer_set_model(AromaNode *node, Aroma3DModel *model);
    Aroma3DModel *aroma_3d_viewer_get_model(AromaNode *node);
    void aroma_3d_viewer_set_camera(AromaNode *node, const Aroma3DCamera *camera);
    void aroma_3d_viewer_reset_camera(AromaNode *node);
    void aroma_3d_viewer_set_auto_rotate(AromaNode *node, bool auto_rotate);
    void aroma_3d_viewer_set_light_position(AromaNode *node, float x, float y, float z);
    void aroma_3d_viewer_update(AromaNode *node);
    void aroma_3d_viewer_draw_deferred(size_t window_id);
    bool aroma_3d_viewer_get_camera(AromaNode *node, Aroma3DCamera *out_camera);
    void aroma_3d_viewer_set_interactive(AromaNode *node, bool interactive);
bool aroma_3d_viewer_get_interactive(AromaNode *node);
#ifdef __cplusplus
}
#endif

#endif