#ifndef SCENARIOS_H
#define SCENARIOS_H

#include <Arduino.h>

typedef enum
{
    SCN_IDLE = 0,
    SCN_HARD_ACCEL,
    SCN_HARD_BRAKE,
    SCN_ACM_FAULT,
    SCN_SCHED_OVERLOAD,
    SCN_SENSOR_FAULT,
    SCN_TURN_SIGNALS,
    SCN_HAZARD_LIGHTS,
    SCN_DOOR_OPEN,
    SCN_SEATBELT_WARN,
    SCN_CRASH,
    SCN_RAIN_WIPER,
    SCN_ENGINE_FAULT,
    SCN_ABS_FAULT,
    SCN_LOW_FUEL,
    SCN_BATTERY_WARN,
    SCN_COMBINED,
    SCN_COUNT
} ScenarioId;

#define VEH_ACCEL_X100_MIN -2000
#define VEH_ACCEL_X100_MAX 2000
#define VEH_SPEED_X10_MAX 2500
#define ACM_BRAKE_PA_MAX 20000
#define ACM_THROTTLE_X100_MAX 10000
#define ACM_FSR_RAW_MAX 4095

#define VEH_CRUISE_SPEED_X10 500

#define ACM_STATUS_NOMINAL 1
#define ACM_STATUS_DEGRADED 2
#define ACM_STATUS_FAULT_SENSOR 3
#define ACM_STATUS_FAULT_ACTUATOR 4




#define VEH_FLAG_DOOR_OPEN      0x0002  
#define VEH_FLAG_ENGINE_FAULT   0x0004  
#define VEH_FLAG_ABS_FAULT      0x0008  
#define VEH_FLAG_LOW_FUEL       0x0010  
#define VEH_FLAG_INDICATOR_L    0x0020  
#define VEH_FLAG_INDICATOR_R    0x0040  
#define VEH_FLAG_CRASH          0x0080  
#define VEH_FLAG_AIRBAG         0x0100  
#define VEH_FLAG_SEATBELT_WARN  0x0200  
#define VEH_FLAG_BATTERY_WARN   0x0400  
#define VEH_FLAG_HARSH_BRAKE    0x1000  
#define VEH_FLAG_HARD_ACCEL     0x2000  


#define FAULT_FLAG_GENERIC        0x01
#define FAULT_FLAG_ACM            0x02
#define FAULT_FLAG_SCHED_OVERLOAD 0x04
#define FAULT_FLAG_SENSOR         0x08
#define FAULT_FLAG_ENGINE         0x10
#define FAULT_FLAG_ABS            0x20
#define FAULT_FLAG_BATTERY        0x40


#define WIPER_OFF   0
#define WIPER_LOW   1
#define WIPER_HIGH  2

static inline const char *scenario_name(ScenarioId id)
{
    switch (id)
    {
    case SCN_IDLE:            return "idle";
    case SCN_HARD_ACCEL:      return "hard_accel";
    case SCN_HARD_BRAKE:      return "hard_brake";
    case SCN_ACM_FAULT:       return "acm_fault";
    case SCN_SCHED_OVERLOAD:  return "sched_overload";
    case SCN_SENSOR_FAULT:    return "sensor_fault";
    case SCN_TURN_SIGNALS:    return "turn_signals";
    case SCN_HAZARD_LIGHTS:   return "hazard_lights";
    case SCN_DOOR_OPEN:       return "door_open";
    case SCN_SEATBELT_WARN:   return "seatbelt_warn";
    case SCN_CRASH:           return "crash";
    case SCN_RAIN_WIPER:      return "rain_wiper";
    case SCN_ENGINE_FAULT:    return "engine_fault";
    case SCN_ABS_FAULT:       return "abs_fault";
    case SCN_LOW_FUEL:        return "low_fuel";
    case SCN_BATTERY_WARN:    return "battery_warn";
    case SCN_COMBINED:        return "combined";
    default:                  return "unknown";
    }
}

static inline bool scenario_from_name(const String &name, ScenarioId *out)
{
    for (int i = 0; i < SCN_COUNT; i++)
    {
        if (name.equalsIgnoreCase(scenario_name((ScenarioId)i)))
        {
            *out = (ScenarioId)i;
            return true;
        }
    }
    return false;
}

static inline float ease_in_out(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

struct ScenarioFrame
{
    int16_t veh_accel_x100;
    uint16_t veh_speed_x10;
    uint16_t veh_flags;
    uint16_t acm_throttle_x100;
    uint16_t acm_brake_pa;
    uint16_t acm_fsr_raw;
    uint8_t acm_status;
    uint8_t fault_flags;
    int16_t env_temp_x10;
    uint16_t env_hum_x100;
    uint8_t gps_satellites;
    uint8_t bcm_wiper_speed;

    uint16_t task0_resp_max_x10us;
    uint16_t task0_deadline_misses;
    uint16_t cpu_load_x100;
    bool done;
};





static inline ScenarioFrame scenario_baseline()
{
    ScenarioFrame f;
    f.veh_accel_x100 = 0;
    f.veh_speed_x10 = 500;
    f.veh_flags = 0;
    f.acm_throttle_x100 = 2000;
    f.acm_brake_pa = 0;
    f.acm_fsr_raw = 400;
    f.acm_status = ACM_STATUS_NOMINAL;
    f.fault_flags = 0;
    f.env_temp_x10 = 220;
    f.env_hum_x100 = 4500;
    f.gps_satellites = 8;
    f.bcm_wiper_speed = WIPER_OFF;
    f.task0_resp_max_x10us = 120;
    f.task0_deadline_misses = 0;
    f.cpu_load_x100 = 2500;
    f.done = false;
    return f;
}

static inline ScenarioFrame scenario_driving_base()
{
    ScenarioFrame f = scenario_baseline();
    f.veh_flags = 0;
    f.bcm_wiper_speed = WIPER_OFF;
    return f;
}

#define HARD_ACCEL_RAMP_MS 400
#define HARD_ACCEL_HOLD_MS 1200
#define HARD_ACCEL_DECAY_MS 600
#define HARD_ACCEL_TOTAL_MS (HARD_ACCEL_RAMP_MS + HARD_ACCEL_HOLD_MS + HARD_ACCEL_DECAY_MS)
#define HARD_ACCEL_PEAK_X100 950

static inline ScenarioFrame scenario_hard_accel(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    f.veh_speed_x10 = 0;
    
    float shape;
    if (elapsed_ms < HARD_ACCEL_RAMP_MS)
    {
        shape = ease_in_out((float)elapsed_ms / HARD_ACCEL_RAMP_MS);
    }
    else if (elapsed_ms < HARD_ACCEL_RAMP_MS + HARD_ACCEL_HOLD_MS)
    {
        shape = 1.0f;
    }
    else if (elapsed_ms < HARD_ACCEL_TOTAL_MS)
    {
        float t = (float)(elapsed_ms - HARD_ACCEL_RAMP_MS - HARD_ACCEL_HOLD_MS) / HARD_ACCEL_DECAY_MS;
        shape = 1.0f - ease_in_out(t);
    }
    else
    {
        shape = 0.0f;
        f.done = true;
    }
    
    f.veh_accel_x100 = (int16_t)clampi((int32_t)(HARD_ACCEL_PEAK_X100 * shape), VEH_ACCEL_X100_MIN, VEH_ACCEL_X100_MAX);
    f.acm_throttle_x100 = (uint16_t)clampi((int32_t)(2000 + shape * 7500), 0, ACM_THROTTLE_X100_MAX);

    float speed_shape = (elapsed_ms < HARD_ACCEL_RAMP_MS) ? shape : 1.0f;
    f.veh_speed_x10 = (uint16_t)clampi((int32_t)(VEH_CRUISE_SPEED_X10 * speed_shape), 0, VEH_SPEED_X10_MAX);
    
    f.veh_flags = (shape > 0.05f) ? VEH_FLAG_HARD_ACCEL : 0;
    
    return f;
}

#define HARD_BRAKE_RAMP_MS 250
#define HARD_BRAKE_HOLD_MS 900
#define HARD_BRAKE_DECAY_MS 500
#define HARD_BRAKE_TOTAL_MS (HARD_BRAKE_RAMP_MS + HARD_BRAKE_HOLD_MS + HARD_BRAKE_DECAY_MS)
#define HARD_BRAKE_PEAK_X100 1100

static inline ScenarioFrame scenario_hard_brake(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    
    float shape;
    if (elapsed_ms < HARD_BRAKE_RAMP_MS)
    {
        shape = ease_in_out((float)elapsed_ms / HARD_BRAKE_RAMP_MS);
    }
    else if (elapsed_ms < HARD_BRAKE_RAMP_MS + HARD_BRAKE_HOLD_MS)
    {
        shape = 1.0f;
    }
    else if (elapsed_ms < HARD_BRAKE_TOTAL_MS)
    {
        float t = (float)(elapsed_ms - HARD_BRAKE_RAMP_MS - HARD_BRAKE_HOLD_MS) / HARD_BRAKE_DECAY_MS;
        shape = 1.0f - ease_in_out(t);
    }
    else
    {
        shape = 0.0f;
        f.done = true;
    }
    
    f.veh_accel_x100 = (int16_t)clampi((int32_t)(-HARD_BRAKE_PEAK_X100 * shape), VEH_ACCEL_X100_MIN, VEH_ACCEL_X100_MAX);
    f.acm_brake_pa = (uint16_t)clampi((int32_t)(shape * ACM_BRAKE_PA_MAX * 0.85f), 0, ACM_BRAKE_PA_MAX);
    f.acm_throttle_x100 = 0;

    float speed_shape = (elapsed_ms < HARD_BRAKE_RAMP_MS) ? (1.0f - shape) : 0.0f;
    f.veh_speed_x10 = (uint16_t)clampi((int32_t)(VEH_CRUISE_SPEED_X10 * speed_shape), 0, VEH_SPEED_X10_MAX);
    
    f.veh_flags = (shape > 0.05f) ? VEH_FLAG_HARSH_BRAKE : 0;
    
    return f;
}

#define ACM_FAULT_HOLD_MS 2000
#define ACM_FAULT_TOTAL_MS ACM_FAULT_HOLD_MS

static inline ScenarioFrame scenario_acm_fault(uint32_t elapsed_ms, uint8_t variant)
{
    ScenarioFrame f = scenario_driving_base();
    
    if (elapsed_ms >= ACM_FAULT_TOTAL_MS)
    {
        f.done = true;
        return f;
    }
    
    f.fault_flags |= FAULT_FLAG_ACM;
    
    if (variant == 0)
    {
        f.acm_status = ACM_STATUS_FAULT_SENSOR;
        f.acm_fsr_raw = ACM_FSR_RAW_MAX;
        f.veh_speed_x10 = 300;
        f.veh_flags = 0;
    }
    else
    {
        f.acm_status = ACM_STATUS_FAULT_ACTUATOR;
        f.acm_throttle_x100 = 6000;
        f.acm_brake_pa = 0;
        f.veh_speed_x10 = 400;
        f.veh_flags = VEH_FLAG_HARD_ACCEL;
    }
    
    return f;
}

#define SCHED_OVERLOAD_RAMP_MS 500
#define SCHED_OVERLOAD_HOLD_MS 2500
#define SCHED_OVERLOAD_TOTAL_MS (SCHED_OVERLOAD_RAMP_MS + SCHED_OVERLOAD_HOLD_MS)

static inline ScenarioFrame scenario_sched_overload(uint32_t elapsed_ms, uint32_t cycle)
{
    ScenarioFrame f = scenario_driving_base();
    
    float shape;
    if (elapsed_ms < SCHED_OVERLOAD_RAMP_MS)
    {
        shape = ease_in_out((float)elapsed_ms / SCHED_OVERLOAD_RAMP_MS);
    }
    else if (elapsed_ms < SCHED_OVERLOAD_TOTAL_MS)
    {
        shape = 1.0f;
    }
    else
    {
        f.done = true;
        return f;
    }
    
    f.cpu_load_x100 = (uint16_t)clampi((int32_t)(2500 + shape * 7000), 0, 10000);
    f.task0_resp_max_x10us = (uint16_t)clampi((int32_t)(120 + shape * 900), 0, 65535);
    f.task0_deadline_misses = (uint16_t)(shape > 0.5f ? (cycle % 8) : 0);
    f.fault_flags |= (shape > 0.8f ? FAULT_FLAG_SCHED_OVERLOAD : 0);
    
    f.veh_speed_x10 = VEH_CRUISE_SPEED_X10;
    f.veh_flags = 0;
    
    return f;
}

#define SENSOR_FAULT_HOLD_MS 2000

static inline ScenarioFrame scenario_sensor_fault(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    
    if (elapsed_ms >= SENSOR_FAULT_HOLD_MS)
    {
        f.done = true;
        return f;
    }
    
    f.env_temp_x10 = 850;
    f.env_hum_x100 = 0;
    f.gps_satellites = 0;
    f.fault_flags |= FAULT_FLAG_SENSOR;
    
    f.veh_speed_x10 = VEH_CRUISE_SPEED_X10;
    f.veh_flags = 0;
    
    return f;
}

#define SIGNAL_TEST_DURATION_MS 3000
#define SIGNAL_BLINK_INTERVAL_MS 500

static inline ScenarioFrame scenario_turn_signals(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    f.veh_speed_x10 = VEH_CRUISE_SPEED_X10;
    
    if (elapsed_ms >= SIGNAL_TEST_DURATION_MS)
    {
        f.done = true;
        return f;
    }
    
    uint32_t phase = (elapsed_ms / SIGNAL_BLINK_INTERVAL_MS) % 4;
    
    switch (phase)
    {
        case 0:  f.veh_flags = VEH_FLAG_INDICATOR_L; break;
        case 1:  f.veh_flags = 0; break;
        case 2:  f.veh_flags = VEH_FLAG_INDICATOR_R; break;
        case 3:  f.veh_flags = 0; break;
    }
    
    return f;
}

#define HAZARD_TEST_DURATION_MS 2500
#define HAZARD_BLINK_INTERVAL_MS 400

static inline ScenarioFrame scenario_hazard_lights(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    f.veh_speed_x10 = 0;
    
    if (elapsed_ms >= HAZARD_TEST_DURATION_MS)
    {
        f.done = true;
        return f;
    }
    
    bool lights_on = ((elapsed_ms / HAZARD_BLINK_INTERVAL_MS) % 2) == 0;
    f.veh_flags = lights_on ? (VEH_FLAG_INDICATOR_L | VEH_FLAG_INDICATOR_R) : 0;
    
    return f;
}

#define DOOR_OPEN_DURATION_MS 2000

static inline ScenarioFrame scenario_door_open(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    f.veh_speed_x10 = 0;
    f.veh_accel_x100 = 0;
    f.acm_throttle_x100 = 0;
    
    if (elapsed_ms >= DOOR_OPEN_DURATION_MS)
    {
        f.done = true;
        return f;
    }
    
    f.veh_flags = VEH_FLAG_DOOR_OPEN;
    
    if (elapsed_ms > 1500)
    {
        f.veh_flags = 0;
    }
    
    return f;
}

#define SEATBELT_WARN_DURATION_MS 2500

static inline ScenarioFrame scenario_seatbelt_warning(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    
    if (elapsed_ms >= SEATBELT_WARN_DURATION_MS)
    {
        f.done = true;
        return f;
    }
    
    f.veh_speed_x10 = (uint16_t)(elapsed_ms * 10 / 100);
    f.veh_speed_x10 = (uint16_t)clampi((int32_t)f.veh_speed_x10, 0, 200);
    f.veh_flags = VEH_FLAG_SEATBELT_WARN;
    
    return f;
}

#define CRASH_DURATION_MS 1500
#define CRASH_IMPACT_MS 300

static inline ScenarioFrame scenario_crash(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    
    if (elapsed_ms >= CRASH_DURATION_MS)
    {
        f.done = true;
        return f;
    }
    
    if (elapsed_ms < CRASH_IMPACT_MS)
    {
        f.veh_speed_x10 = 600;
        f.veh_accel_x100 = 0;
        f.veh_flags = 0;
    }
    else if (elapsed_ms < CRASH_IMPACT_MS + 200)
    {
        f.veh_speed_x10 = 100;
        f.veh_accel_x100 = -2000;
        f.veh_flags = VEH_FLAG_CRASH | VEH_FLAG_AIRBAG;
        f.acm_brake_pa = ACM_BRAKE_PA_MAX;
    }
    else
    {
        f.veh_speed_x10 = 0;
        f.veh_accel_x100 = 0;
        f.veh_flags = VEH_FLAG_CRASH | VEH_FLAG_AIRBAG;
        f.acm_brake_pa = ACM_BRAKE_PA_MAX;
    }
    
    return f;
}

#define WIPER_TEST_DURATION_MS 3000

static inline ScenarioFrame scenario_rain_wiper(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    f.veh_speed_x10 = VEH_CRUISE_SPEED_X10;
    
    if (elapsed_ms >= WIPER_TEST_DURATION_MS)
    {
        f.done = true;
        return f;
    }
    
    f.env_temp_x10 = 150;
    f.env_hum_x100 = 9500;
    
    if (elapsed_ms < 800)
    {
        f.bcm_wiper_speed = WIPER_OFF;
    }
    else if (elapsed_ms < 1600)
    {
        f.bcm_wiper_speed = WIPER_LOW;
    }
    else
    {
        f.bcm_wiper_speed = WIPER_HIGH;
    }
    
    return f;
}





#define ENGINE_FAULT_RAMP_MS  600
#define ENGINE_FAULT_HOLD_MS  2000
#define ENGINE_FAULT_TOTAL_MS (ENGINE_FAULT_RAMP_MS + ENGINE_FAULT_HOLD_MS)

static inline ScenarioFrame scenario_engine_fault(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    
    if (elapsed_ms >= ENGINE_FAULT_TOTAL_MS)
    {
        f.done = true;
        return f;
    }
    
    float t = (float)elapsed_ms / ENGINE_FAULT_TOTAL_MS;
    f.veh_speed_x10 = (uint16_t)(400 + 100.0f * sin(t * 10.0f));
    f.veh_accel_x100 = (int16_t)(-300.0f + 200.0f * sin(t * 15.0f));
    f.veh_flags = VEH_FLAG_ENGINE_FAULT;
    f.fault_flags |= FAULT_FLAG_ENGINE;
    f.acm_throttle_x100 = (uint16_t)(1500 + 500.0f * sin(t * 8.0f));
    f.env_temp_x10 = 950;
    
    return f;
}

#define ABS_FAULT_HOLD_MS 2500

static inline ScenarioFrame scenario_abs_fault(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    f.veh_speed_x10 = 600;
    
    if (elapsed_ms >= ABS_FAULT_HOLD_MS)
    {
        f.done = true;
        return f;
    }
    
    f.veh_flags = VEH_FLAG_ABS_FAULT;
    f.fault_flags |= FAULT_FLAG_ABS;
    f.acm_brake_pa = 5000;
    f.acm_status = ACM_STATUS_DEGRADED;
    
    if (elapsed_ms > 1200)
    {
        f.veh_accel_x100 = (int16_t)(-400 + 200.0f * sin(elapsed_ms * 0.02f));
        f.veh_speed_x10 = (uint16_t)(600 - (elapsed_ms - 1200) / 5);
    }
    
    return f;
}

#define LOW_FUEL_DURATION_MS 2000

static inline ScenarioFrame scenario_low_fuel(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    f.veh_speed_x10 = 450;
    
    if (elapsed_ms >= LOW_FUEL_DURATION_MS)
    {
        f.done = true;
        return f;
    }
    
    f.veh_flags = VEH_FLAG_LOW_FUEL;
    
    if (elapsed_ms > 1200)
    {
        f.veh_speed_x10 = (uint16_t)(450 - (elapsed_ms - 1200) / 4);
        f.veh_speed_x10 = (uint16_t)clampi((int32_t)f.veh_speed_x10, 0, VEH_SPEED_X10_MAX);
        f.veh_accel_x100 = -300;
        f.acm_throttle_x100 = 500;
    }
    
    return f;
}

#define BATTERY_WARN_DURATION_MS 3000

static inline ScenarioFrame scenario_battery_warn(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    f.veh_speed_x10 = VEH_CRUISE_SPEED_X10;
    
    if (elapsed_ms >= BATTERY_WARN_DURATION_MS)
    {
        f.done = true;
        return f;
    }
    
    f.veh_flags = VEH_FLAG_BATTERY_WARN;
    f.fault_flags |= FAULT_FLAG_BATTERY;
    
    if (elapsed_ms > 1500)
    {
        f.gps_satellites = 2;
        f.env_temp_x10 = 250;
        f.acm_fsr_raw = (uint16_t)(200 + 100.0f * sin(elapsed_ms * 0.05f));
    }
    
    return f;
}

#define COMBINED_TEST_DURATION_MS 5000

static inline ScenarioFrame scenario_combined_driving(uint32_t elapsed_ms)
{
    ScenarioFrame f = scenario_driving_base();
    f.veh_speed_x10 = VEH_CRUISE_SPEED_X10;
    f.bcm_wiper_speed = WIPER_LOW;
    
    if (elapsed_ms >= COMBINED_TEST_DURATION_MS)
    {
        f.done = true;
        return f;
    }
    
    f.env_hum_x100 = 8000;
    
    uint32_t phase = elapsed_ms / 1000;
    
    switch (phase)
    {
        case 0:
            f.veh_flags = 0;
            break;
        case 1:
            f.veh_flags = ((elapsed_ms / 500) % 2) ? VEH_FLAG_INDICATOR_L : 0;
            break;
        case 2:
            f.veh_flags = 0;
            break;
        case 3:
            f.veh_flags = ((elapsed_ms / 500) % 2) ? VEH_FLAG_INDICATOR_R : 0;
            f.veh_accel_x100 = 200;
            f.veh_speed_x10 = 600;
            break;
        case 4:
            f.veh_flags = 0;
            f.veh_accel_x100 = 0;
            f.veh_speed_x10 = VEH_CRUISE_SPEED_X10;
            break;
    }
    
    return f;
}

#endif 