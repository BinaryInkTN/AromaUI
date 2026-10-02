#ifndef VEHICLE_CAMERA_H
#define VEHICLE_CAMERA_H

#include "aroma.h"

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

void apply_vehicle_model_to_viewer(Aroma3DModel *model, float metallic, float clearcoat);
void lock_vehicle_camera(const Aroma3DCamera *cam);
void set_exterior_overlays_hidden(bool hidden);
void ac_interior_callback(void *user_data);
void close_interior_view_callback(void *user_data);
bool car_frontdoor_open(AromaNode *node, void *user_data);
void tire_cycle_callback(void *user_data);
void tire_exit_check_callback(void *user_data);
void tire_next_callback(void *user_data);
bool tire_select_callback(AromaNode *btn, void *user_data);

extern bool interior_view_active;
extern AromaNode *interior_close_btn;
extern Aroma3DCamera locked_vehicle_camera;
extern bool has_locked_vehicle_camera;
extern Aroma3DLoadJob *pending_vehicle_model_job;
extern AromaNode *vehicle_model_loading_spinner;

typedef void (*VehicleModelLoadedCb)(bool success, void *user_data);
extern VehicleModelLoadedCb vehicle_model_loaded_cb;
extern void *vehicle_model_loaded_cb_user_data;

void set_vehicle_model_loaded_callback(VehicleModelLoadedCb cb, void *user_data);

#endif
