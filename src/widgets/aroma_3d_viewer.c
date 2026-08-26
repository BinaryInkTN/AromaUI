#include "widgets/aroma_3d_viewer.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_logger.h"
#include "core/aroma_node.h"
#include "aroma_3d.h"
#include "aroma_event.h"
#include <string.h>
#include <math.h>

struct Aroma3DViewer
{
    AromaRect rect;
    Aroma3DModel *model;
    Aroma3DCamera camera;
    bool is_dragging;
    bool auto_rotate;
    bool interactive;
    float last_x;
    float last_y;
};

static bool point_in_rect(const AromaRect *rect, float x, float y)
{
    return rect && x >= rect->x && x < (rect->x + rect->width) &&
           y >= rect->y && y < (rect->y + rect->height);
}

static bool viewer_event_handler(AromaEvent *event, void *user_data)
{
    AromaNode *node = (AromaNode *)user_data;
    if (!node || node->node_widget_ptr == NULL)
        return false;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;

    if (!viewer->interactive)
        return false;

    switch (event->event_type)
    {
    case EVENT_TYPE_MOUSE_MOVE:
    case EVENT_TYPE_TOUCH_MOVE:
        if (viewer->is_dragging)
        {
            float dx = (float)event->data.mouse.x - viewer->last_x;
            float dy = (float)event->data.mouse.y - viewer->last_y;
            aroma_3d_camera_orbit(&viewer->camera, dx, dy);
            viewer->last_x = (float)event->data.mouse.x;
            viewer->last_y = (float)event->data.mouse.y;
            aroma_node_invalidate(node);
        }
        break;

    case EVENT_TYPE_MOUSE_CLICK:
    case EVENT_TYPE_TOUCH_DOWN:
        if (!point_in_rect(aroma_node_get_rect(node), (float)event->data.mouse.x, (float)event->data.mouse.y))
            break;
        viewer->is_dragging = true;
        viewer->last_x = (float)event->data.mouse.x;
        viewer->last_y = (float)event->data.mouse.y;
        break;

    case EVENT_TYPE_MOUSE_RELEASE:
    case EVENT_TYPE_TOUCH_UP:
        viewer->is_dragging = false;
        break;

    case EVENT_TYPE_MOUSE_SCROLL:
        aroma_3d_camera_zoom(&viewer->camera, event->data.mouse.scroll_y);
        aroma_node_invalidate(node);
        break;

    default:
        break;
    }
    return false;
}

static void viewer_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr)
        return;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;

    AromaRect *rect = aroma_node_get_rect(node);
    if (!rect || rect->width <= 0 || rect->height <= 0)
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle)
        return;

    if (!viewer->model)
    {
        gfx->fill_rectangle(window_id, rect->x, rect->y, rect->width, rect->height,
                            0xFF1A1A2E, false, 0.0f);
        return;
    }

    if (viewer->auto_rotate && !viewer->is_dragging)
    {
        aroma_3d_camera_orbit(&viewer->camera, 0.5f, 0.0f);
        aroma_node_invalidate(node);
    }

    int win_w = 0, win_h = 0;
    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_window_size)
        platform->get_window_size(window_id, &win_w, &win_h);

    aroma_3d_render_to_rect(viewer->model, &viewer->camera, rect->x, rect->y, rect->width, rect->height, win_w, win_h);
}

AromaNode *aroma_3d_viewer_create(AromaNode *parent, int x, int y, int width, int height)
{
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)aroma_widget_alloc(sizeof(struct Aroma3DViewer));
    if (!viewer)
        return NULL;
    memset(viewer, 0, sizeof(struct Aroma3DViewer));
    viewer->rect.x = x;
    viewer->rect.y = y;
    viewer->rect.width = width;
    viewer->rect.height = height;
    aroma_3d_camera_init(&viewer->camera);
    viewer->camera.radius = 3.0f;
    viewer->interactive = true;

    AromaNode *viewer_node = __add_child_node(NODE_TYPE_WIDGET, parent, viewer);
    if (!viewer_node)
    {
        aroma_widget_free(viewer);
        return NULL;
    }

    aroma_node_set_draw_cb(viewer_node, viewer_draw);

    aroma_event_subscribe(viewer_node->node_id, EVENT_TYPE_MOUSE_MOVE, viewer_event_handler, viewer_node, 80);
    aroma_event_subscribe(viewer_node->node_id, EVENT_TYPE_MOUSE_CLICK, viewer_event_handler, viewer_node, 90);
    aroma_event_subscribe(viewer_node->node_id, EVENT_TYPE_MOUSE_RELEASE, viewer_event_handler, viewer_node, 90);
    aroma_event_subscribe(viewer_node->node_id, EVENT_TYPE_TOUCH_DOWN, viewer_event_handler, viewer_node, 90);
    aroma_event_subscribe(viewer_node->node_id, EVENT_TYPE_TOUCH_UP, viewer_event_handler, viewer_node, 90);
    aroma_event_subscribe(viewer_node->node_id, EVENT_TYPE_TOUCH_MOVE, viewer_event_handler, viewer_node, 80);
    aroma_event_subscribe(viewer_node->node_id, EVENT_TYPE_MOUSE_SCROLL, viewer_event_handler, viewer_node, 90);
    aroma_event_subscribe(viewer_node->node_id, EVENT_TYPE_MOUSE_EXIT, viewer_event_handler, viewer_node, 80);

    return viewer_node;
}

void aroma_3d_viewer_set_model(AromaNode *node, Aroma3DModel *model)
{
    if (!node || !node->node_widget_ptr)
        return;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;
    if (viewer->model == model)
        return;
    aroma_3d_destroy_model(viewer->model);
    viewer->model = model;
    aroma_3d_camera_init(&viewer->camera);
    viewer->camera.radius = 3.0f;

    if (model)
    {
        float bmin[3], bmax[3];
        aroma_3d_get_model_bounds(model, bmin, bmax);
        viewer->camera.target[0] = (bmin[0] + bmax[0]) * 0.5f;
        viewer->camera.target[1] = (bmin[1] + bmax[1]) * 0.5f;
        viewer->camera.target[2] = (bmin[2] + bmax[2]) * 0.5f;
        float dx = bmax[0] - bmin[0];
        float dy = bmax[1] - bmin[1];
        float dz = bmax[2] - bmin[2];
        float max_dim = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
        viewer->camera.radius = max_dim * 1.5f;
        if (viewer->camera.radius < 1.0f)
            viewer->camera.radius = 1.0f;
    }

    aroma_node_invalidate(node);
}

Aroma3DModel *aroma_3d_viewer_get_model(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return NULL;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;
    return viewer->model;
}

void aroma_3d_viewer_set_camera(AromaNode *node, const Aroma3DCamera *camera)
{
    if (!node || !node->node_widget_ptr || !camera)
        return;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;
    memcpy(&viewer->camera, camera, sizeof(Aroma3DCamera));
    aroma_node_invalidate(node);
}

void aroma_3d_viewer_reset_camera(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;
    aroma_3d_camera_init(&viewer->camera);
    viewer->camera.radius = 3.0f;
    aroma_node_invalidate(node);
}

void aroma_3d_viewer_set_auto_rotate(AromaNode *node, bool auto_rotate)
{
    if (!node || !node->node_widget_ptr)
        return;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;
    viewer->auto_rotate = auto_rotate;
    if (auto_rotate)
        aroma_node_invalidate(node);
}

void aroma_3d_viewer_set_interactive(AromaNode *node, bool interactive)
{
    if (!node || !node->node_widget_ptr)
        return;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;
    viewer->interactive = interactive;
    if (!interactive)
        viewer->is_dragging = false;
}

bool aroma_3d_viewer_get_interactive(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return false;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;
    return viewer->interactive;
}

void aroma_3d_viewer_set_light_position(AromaNode *node, float x, float y, float z)
{
    if (!node || !node->node_widget_ptr)
        return;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;
    aroma_3d_set_light_position(x, y, z);
    aroma_node_invalidate(node);
}

void aroma_3d_viewer_update(AromaNode *node)
{
    if (!node)
        return;
    aroma_node_invalidate(node);
}

bool aroma_3d_viewer_get_camera(AromaNode *node, Aroma3DCamera *out_camera)
{
    if (!node || !node->node_widget_ptr || !out_camera)
        return false;
    struct Aroma3DViewer *viewer = (struct Aroma3DViewer *)node->node_widget_ptr;
    memcpy(out_camera, &viewer->camera, sizeof(Aroma3DCamera));
    return true;
}

AromaNodeDrawFn aroma_3d_viewer_get_draw_cb(void)
{
    return viewer_draw;
}