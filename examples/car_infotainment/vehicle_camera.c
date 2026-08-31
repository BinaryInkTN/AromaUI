#include "vehicle_view.h"
#include "media_controls.h"
#include "aroma_animation.h"
#include <string.h>
#include <math.h>

#define VEHICLE_CAM_EXTERIOR_THETA -5.0f
#define VEHICLE_CAM_EXTERIOR_PHI 1.34f
#define VEHICLE_CAM_EXTERIOR_RADIUS 5.14f
#define VEHICLE_CAM_EXTERIOR_TARGET_X 0.05f
#define VEHICLE_CAM_EXTERIOR_TARGET_Y -0.13f
#define VEHICLE_CAM_EXTERIOR_TARGET_Z -0.08f

#define VEHICLE_CAM_INTERIOR_THETA -3.10f
#define VEHICLE_CAM_INTERIOR_PHI 1.09f
#define VEHICLE_CAM_INTERIOR_RADIUS 1.14f
#define VEHICLE_CAM_INTERIOR_TARGET_X 0.05f
#define VEHICLE_CAM_INTERIOR_TARGET_Y -0.13f
#define VEHICLE_CAM_INTERIOR_TARGET_Z -0.08f

#define VEHICLE_CAM_FRONT_THETA (VEHICLE_CAM_EXTERIOR_THETA + 1.5708f)
#define VEHICLE_CAM_FRONT_PHI VEHICLE_CAM_EXTERIOR_PHI
#define VEHICLE_CAM_FRONT_RADIUS VEHICLE_CAM_EXTERIOR_RADIUS
#define VEHICLE_CAM_FRONT_TARGET_X VEHICLE_CAM_EXTERIOR_TARGET_X
#define VEHICLE_CAM_FRONT_TARGET_Y VEHICLE_CAM_EXTERIOR_TARGET_Y
#define VEHICLE_CAM_FRONT_TARGET_Z VEHICLE_CAM_EXTERIOR_TARGET_Z

#define VEHICLE_CAM_REAR_THETA (VEHICLE_CAM_FRONT_THETA + 3.14159f)
#define VEHICLE_CAM_REAR_PHI VEHICLE_CAM_EXTERIOR_PHI
#define VEHICLE_CAM_REAR_RADIUS VEHICLE_CAM_EXTERIOR_RADIUS
#define VEHICLE_CAM_REAR_TARGET_X VEHICLE_CAM_EXTERIOR_TARGET_X
#define VEHICLE_CAM_REAR_TARGET_Y VEHICLE_CAM_EXTERIOR_TARGET_Y
#define VEHICLE_CAM_REAR_TARGET_Z VEHICLE_CAM_EXTERIOR_TARGET_Z

#define VEHICLE_CAM_TIRE_THETA 0.0f
#define VEHICLE_CAM_TIRE_PHI 1.2f
#define VEHICLE_CAM_TIRE_RADIUS 2.2f

static const struct
{
    const char *name;
    float target_x;
    float target_y;
    float target_z;
    float theta;
} tire_views[] = {
    {"Front Right", -0.85f, -0.08f, 1.05f, -2.2f},
    {"Front Left", 0.85f, -0.08f, 1.05f, 2.2f},
    {"Rear Right", -0.85f, -0.08f, -1.05f, -2.2f},
    {"Rear Left", 0.85f, -0.08f, -1.05f, 2.2f},
};

bool interior_view_active = false;
AromaNode *interior_close_btn = NULL;

Aroma3DCamera locked_vehicle_camera;
bool has_locked_vehicle_camera = false;

Aroma3DLoadJob *pending_vehicle_model_job = NULL;
AromaNode *vehicle_model_loading_spinner = NULL;

typedef void (*VehicleModelLoadedCb)(bool success, void *user_data);
VehicleModelLoadedCb vehicle_model_loaded_cb = NULL;
void *vehicle_model_loaded_cb_user_data = NULL;

void set_vehicle_model_loaded_callback(VehicleModelLoadedCb cb, void *user_data)
{
    vehicle_model_loaded_cb = cb;
    vehicle_model_loaded_cb_user_data = user_data;
}

static void apply_vehicle_paint_finish(Aroma3DModel *model, float metallic, float clearcoat)
{
    if (!model)
        return;
    int mesh_count = aroma_3d_get_mesh_count(model);
    for (int i = 0; i < mesh_count; i++)
    {
        const Aroma3DMesh *mesh = aroma_3d_get_mesh(model, i);
        if (aroma_3d_mesh_is_alpha_blended(mesh))
            continue;
        aroma_3d_set_mesh_metallic(model, i, metallic);
        aroma_3d_set_mesh_clearcoat(model, i, clearcoat);
    }
}

void apply_vehicle_model_to_viewer(Aroma3DModel *model, float metallic, float clearcoat)
{
    if (!state.viewer_3d || !model)
        return;

    apply_vehicle_paint_finish(model, metallic, clearcoat);
    aroma_3d_viewer_set_model(state.viewer_3d, model);

    Aroma3DCamera cam;
    aroma_3d_viewer_get_camera(state.viewer_3d, &cam);

    cam.theta = VEHICLE_CAM_FRONT_THETA;
    cam.phi = VEHICLE_CAM_FRONT_PHI;
    cam.radius = VEHICLE_CAM_FRONT_RADIUS;
    cam.target[0] = VEHICLE_CAM_FRONT_TARGET_X;
    cam.target[1] = VEHICLE_CAM_FRONT_TARGET_Y;
    cam.target[2] = VEHICLE_CAM_FRONT_TARGET_Z;

    cam.fov = 45.0f;
    cam.near_plane = 0.1f;
    cam.far_plane = 1000.0f;
    lock_vehicle_camera(&cam);

    state.startup_animating = true;
    state.camera_animating = true;
    state.anim_target_theta = VEHICLE_CAM_EXTERIOR_THETA;
    state.anim_target_phi = VEHICLE_CAM_EXTERIOR_PHI;
    state.anim_target_radius = VEHICLE_CAM_EXTERIOR_RADIUS;
    state.anim_target_x = VEHICLE_CAM_EXTERIOR_TARGET_X;
    state.anim_target_y = VEHICLE_CAM_EXTERIOR_TARGET_Y;
    state.anim_target_z = VEHICLE_CAM_EXTERIOR_TARGET_Z;

    aroma_3d_viewer_set_auto_rotate(state.viewer_3d, false);
    aroma_3d_viewer_set_light_position(state.viewer_3d, 4.0f, 6.0f, 3.0f);
}

void lock_vehicle_camera(const Aroma3DCamera *cam)
{
    memcpy(&locked_vehicle_camera, cam, sizeof(Aroma3DCamera));
    has_locked_vehicle_camera = true;
    if (state.viewer_3d)
        aroma_3d_viewer_set_camera(state.viewer_3d, cam);
}

void set_exterior_overlays_hidden(bool hidden)
{
    aroma_node_set_hidden(state.battery_button, hidden);
    aroma_node_set_hidden(state.vehicle_view_clock_gauge, hidden);
    aroma_node_set_hidden(state.vehicle_view_ampm_label, hidden);
    aroma_node_set_hidden(state.tire_button, hidden);
    aroma_node_set_hidden(media_ui.media_card, hidden);
    aroma_node_set_hidden(state.bottom_bar, hidden);
    aroma_node_set_hidden(state.status_card, hidden);
    aroma_node_set_hidden(state.wifi_icon, hidden); 
    aroma_node_set_hidden(state.gps_icon, hidden);
    aroma_node_set_hidden(state.signal_icon, hidden); 
    aroma_node_set_hidden(state.battery_icon, hidden);
}

void ac_interior_callback(void *user_data)
{
    (void)user_data;
    if (!state.viewer_3d || interior_view_active || state.startup_animating)
        return;

    state.anim_target_theta = VEHICLE_CAM_INTERIOR_THETA;
    state.anim_target_phi = VEHICLE_CAM_INTERIOR_PHI;
    state.anim_target_radius = VEHICLE_CAM_INTERIOR_RADIUS;
    state.anim_target_x = VEHICLE_CAM_INTERIOR_TARGET_X;
    state.anim_target_y = VEHICLE_CAM_INTERIOR_TARGET_Y;
    state.anim_target_z = VEHICLE_CAM_INTERIOR_TARGET_Z;
    state.camera_animating = true;

    Aroma3DCamera camera;
    aroma_3d_viewer_get_camera(state.viewer_3d, &camera);
    camera.far_plane = 1000.0f;
    camera.near_plane = 0.1f;
    camera.fov = 45.0f;
    aroma_3d_viewer_set_camera(state.viewer_3d, &camera);

    interior_view_active = true;
    set_exterior_overlays_hidden(true);

    if (interior_close_btn)
        aroma_node_set_hidden(interior_close_btn, false);
    if (state.ac_controls_btn)
        aroma_node_set_hidden(state.ac_controls_btn, false);
    if (state.seat_controls_btn)
        aroma_node_set_hidden(state.seat_controls_btn, false);
    if (state.ac_controls_card)
        aroma_node_set_hidden(state.ac_controls_card, true);
    if (state.seat_controls_card)
        aroma_node_set_hidden(state.seat_controls_card, true);
    if (state.tire_card)
        aroma_node_set_hidden(state.tire_card, true);
    if(state.interior_ac_btn)
        aroma_node_set_hidden(state.interior_ac_btn, true);
    }


void close_interior_view_callback(void *user_data)
{
    (void)user_data;
    if (!state.viewer_3d || !interior_view_active || state.startup_animating)
        return;

    state.anim_target_theta = VEHICLE_CAM_EXTERIOR_THETA;
    state.anim_target_phi = VEHICLE_CAM_EXTERIOR_PHI;
    state.anim_target_radius = VEHICLE_CAM_EXTERIOR_RADIUS;
    state.anim_target_x = VEHICLE_CAM_EXTERIOR_TARGET_X;
    state.anim_target_y = VEHICLE_CAM_EXTERIOR_TARGET_Y;
    state.anim_target_z = VEHICLE_CAM_EXTERIOR_TARGET_Z;
    state.camera_animating = true;

    interior_view_active = false;
    set_exterior_overlays_hidden(false);

    if (interior_close_btn)
        aroma_node_set_hidden(interior_close_btn, true);
    if (state.ac_controls_btn)
        aroma_node_set_hidden(state.ac_controls_btn, true);
    if (state.ac_controls_card)
        aroma_node_set_hidden(state.ac_controls_card, true);
    if (state.ac_temp_up_btn)
        aroma_node_set_hidden(state.ac_temp_up_btn, true);
    if (state.ac_temp_down_btn)
        aroma_node_set_hidden(state.ac_temp_down_btn, true);
    if (state.fan_up_btn)
        aroma_node_set_hidden(state.fan_up_btn, true);
    if (state.fan_down_btn)
        aroma_node_set_hidden(state.fan_down_btn, true);
    if (state.ac_mode_btn)
        aroma_node_set_hidden(state.ac_mode_btn, true);
    if (state.ac_power_btn)
        aroma_node_set_hidden(state.ac_power_btn, true);
    if (state.seat_controls_btn)
        aroma_node_set_hidden(state.seat_controls_btn, true);
    if (state.seat_controls_card)
        aroma_node_set_hidden(state.seat_controls_card, true);
    if (state.tire_card)
        aroma_node_set_hidden(state.tire_card, true);
    if(state.interior_ac_btn)
        aroma_node_set_hidden(state.interior_ac_btn, false);
}

bool car_frontdoor_open(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    aroma_image_set_source(state.overlay,
#ifdef __EMSCRIPTEN__
                           "/assets/car_frontdoor.png"
#elif defined(__arm__) || defined(__aarch64__)
                           "/usr/share/infotainment/assets/car_frontdoor.png"
#else
                           "../assets/car_frontdoor.png"
#endif
    );
    return true;
}

void tire_cycle_callback(void *user_data)
{
    (void)user_data;
    if (!state.viewer_3d || interior_view_active || state.startup_animating)
        return;

    if (state.tire_check_active)
    {
        tire_exit_check_callback(user_data);
        return;
    }

    state.tire_check_active = true;
    state.selected_tire = 0;
    set_exterior_overlays_hidden(true);

    if (interior_close_btn)
        aroma_node_set_hidden(interior_close_btn, true);
    if (state.interior_ac_btn)
        aroma_node_set_hidden(state.interior_ac_btn, true);
    if (state.ac_controls_btn)
        aroma_node_set_hidden(state.ac_controls_btn, true);
    if (state.ac_controls_card)
        aroma_node_set_hidden(state.ac_controls_card, true);
    if (state.ac_temp_up_btn)
        aroma_node_set_hidden(state.ac_temp_up_btn, true);
    if (state.ac_temp_down_btn)
        aroma_node_set_hidden(state.ac_temp_down_btn, true);
    if (state.fan_up_btn)
        aroma_node_set_hidden(state.fan_up_btn, true);
    if (state.fan_down_btn)
        aroma_node_set_hidden(state.fan_down_btn, true);
    if (state.ac_mode_btn)
        aroma_node_set_hidden(state.ac_mode_btn, true);
    if (state.ac_power_btn)
        aroma_node_set_hidden(state.ac_power_btn, true);
    if (state.seat_controls_btn)
        aroma_node_set_hidden(state.seat_controls_btn, true);
    if (state.seat_controls_card)
        aroma_node_set_hidden(state.seat_controls_card, true);
    if (state.tire_card)
        aroma_node_set_hidden(state.tire_card, true);

    if(state.interior_ac_btn)
        aroma_node_set_hidden(state.interior_ac_btn, true);

    state.anim_target_theta = tire_views[0].theta;
    state.anim_target_phi = VEHICLE_CAM_TIRE_PHI;
    state.anim_target_radius = VEHICLE_CAM_TIRE_RADIUS;
    state.anim_target_x = tire_views[0].target_x;
    state.anim_target_y = tire_views[0].target_y;
    state.anim_target_z = tire_views[0].target_z;
    state.camera_animating = true;

    if (state.tire_card)
    {
        aroma_label_set_text(state.tire_name_label, tire_views[0].name);
        char pressure_buf[64];
        snprintf(pressure_buf, sizeof(pressure_buf), "%d psi", 28);
        aroma_label_set_text(state.tire_pressure_label, pressure_buf);
        aroma_node_set_hidden(state.tire_card, false);
    }
}

void tire_next_callback(void *user_data)
{
    (void)user_data;
    if (!state.tire_check_active || !state.viewer_3d || interior_view_active || state.startup_animating)
        return;

    state.selected_tire = (state.selected_tire + 1) % 4;

    state.anim_target_theta = tire_views[state.selected_tire].theta;
    state.anim_target_phi = VEHICLE_CAM_TIRE_PHI;
    state.anim_target_radius = VEHICLE_CAM_TIRE_RADIUS;
    state.anim_target_x = tire_views[state.selected_tire].target_x;
    state.anim_target_y = tire_views[state.selected_tire].target_y;
    state.anim_target_z = tire_views[state.selected_tire].target_z;
    state.camera_animating = true;

    if (state.tire_card)
    {
        aroma_label_set_text(state.tire_name_label, tire_views[state.selected_tire].name);
        char pressure_buf[64];
        snprintf(pressure_buf, sizeof(pressure_buf), "%d psi", 28 + state.selected_tire * 2);
        aroma_label_set_text(state.tire_pressure_label, pressure_buf);
    }
}

bool tire_select_callback(AromaNode *btn, void *user_data)
{
    (void)btn;
    if (!state.tire_check_active || !state.viewer_3d || interior_view_active || state.startup_animating)
        return true;

    int tire_idx = (int)(intptr_t)user_data;
    if (tire_idx < 0 || tire_idx >= 4)
        return true;

    state.selected_tire = tire_idx;

    state.anim_target_theta = tire_views[tire_idx].theta;
    state.anim_target_phi = VEHICLE_CAM_TIRE_PHI;
    state.anim_target_radius = VEHICLE_CAM_TIRE_RADIUS;
    state.anim_target_x = tire_views[tire_idx].target_x;
    state.anim_target_y = tire_views[tire_idx].target_y;
    state.anim_target_z = tire_views[tire_idx].target_z;
    state.camera_animating = true;

    if (state.tire_card)
    {
        aroma_label_set_text(state.tire_name_label, tire_views[tire_idx].name);
        char pressure_buf[64];
        snprintf(pressure_buf, sizeof(pressure_buf), "%d psi", 28 + tire_idx * 2);
        aroma_label_set_text(state.tire_pressure_label, pressure_buf);
    }
    return true;
}

void tire_exit_check_callback(void *user_data)
{
    (void)user_data;
    if (!state.tire_check_active)
        return;

    state.tire_check_active = false;
    state.selected_tire = 0;

    state.anim_target_theta = VEHICLE_CAM_EXTERIOR_THETA;
    state.anim_target_phi = VEHICLE_CAM_EXTERIOR_PHI;
    state.anim_target_radius = VEHICLE_CAM_EXTERIOR_RADIUS;
    state.anim_target_x = VEHICLE_CAM_EXTERIOR_TARGET_X;
    state.anim_target_y = VEHICLE_CAM_EXTERIOR_TARGET_Y;
    state.anim_target_z = VEHICLE_CAM_EXTERIOR_TARGET_Z;
    state.camera_animating = true;

    set_exterior_overlays_hidden(false);

    if (state.ac_controls_btn)
        aroma_node_set_hidden(state.ac_controls_btn, true);
    if (state.seat_controls_btn)
        aroma_node_set_hidden(state.seat_controls_btn, true);
    if (interior_close_btn)
        aroma_node_set_hidden(interior_close_btn, true);
    if (state.ac_controls_card)
        aroma_node_set_hidden(state.ac_controls_card, true);
    if (state.ac_temp_up_btn)
        aroma_node_set_hidden(state.ac_temp_up_btn, true);
    if (state.ac_temp_down_btn)
        aroma_node_set_hidden(state.ac_temp_down_btn, true);
    if (state.fan_up_btn)
        aroma_node_set_hidden(state.fan_up_btn, true);
    if (state.fan_down_btn)
        aroma_node_set_hidden(state.fan_down_btn, true);
    if (state.ac_mode_btn)
        aroma_node_set_hidden(state.ac_mode_btn, true);
    if (state.ac_power_btn)
        aroma_node_set_hidden(state.ac_power_btn, true);
    if (state.seat_controls_card)
        aroma_node_set_hidden(state.seat_controls_card, true);
    if (state.tire_card)
        aroma_node_set_hidden(state.tire_card, true);
    if(state.interior_ac_btn)
        aroma_node_set_hidden(state.interior_ac_btn, false);
    }
