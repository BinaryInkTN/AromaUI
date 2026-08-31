#include <unistd.h>

#include "main_loop.h"
#include "app_state.h"
#include "voice_handler.h"
#include "aroma.h"
#include "aroma_animation.h"
#include "widgets/aroma_gauge.h"
#include <time.h>
#include "vehicle_view.h"
#include <stdio.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static const char *get_fault_message(uint32_t fault)
{
    static const struct
    {
        uint32_t code;
        const char *message;
    } fault_table[] = {
        {0xB101, "BMS: Cell Overvoltage"},
        {0xB102, "BMS: Cell Undervoltage"},
        {0xB100, "BMS: Isolation Fault"},
        {0xC201, "Motor: Inverter Overtemp"},
        {0xC200, "Motor: Drive Inverter Fault"},
        {0xA100, "Battery: Critical Low"},
        {0xD300, "HVAC: Compressor Fault"},
        {0xD301, "HVAC: Coolant Pump Fault"},
        {0xE400, "Autopilot: Camera Blinded"},
        {0xE401, "Autopilot: Radar Fault"}};

    for (size_t i = 0; i < sizeof(fault_table) / sizeof(fault_table[0]); i++)
    {
        if (fault_table[i].code == fault)
        {
            return fault_table[i].message;
        }
    }
    return "Unknown Error";
}

static void update_label_safe(AromaNode *label, const char *text)
{
    if (label && safe_node_check((const AromaNode *)label, "label"))
    {
        aroma_label_set_text(label, text);
    }
}


void main_loop()
{    

    
    uint64_t last_time_update = aroma_time_now_ms();

    if (state.vehicle_view_clock_gauge)
    {
        time_t rawtime;
        struct tm *timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        if (timeinfo)
        {
            float minute_val = (float)timeinfo->tm_min + (float)timeinfo->tm_sec / 60.0f;
            aroma_gauge_set_value(state.vehicle_view_clock_gauge, minute_val);
            aroma_gauge_set_secondary_value(state.vehicle_view_clock_gauge, (float)timeinfo->tm_sec);
            aroma_gauge_set_extra_value(state.vehicle_view_clock_gauge, (float)(timeinfo->tm_hour % 12) * 5.0f + (float)timeinfo->tm_min / 12.0f);
            if (state.vehicle_view_ampm_label)
            {
                const char *ampm = timeinfo->tm_hour >= 12 ? "PM" : "AM";
                update_label_safe(state.vehicle_view_ampm_label, ampm);
            }
        }
    }

    while (aroma_ui_is_running())
    {
        
        uint64_t now = aroma_time_now_ms();

        if (state.vehicle_view_clock_gauge)
        {
            time_t rawtime;
            struct tm *timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            if (timeinfo)
            {
                float minute_val = (float)timeinfo->tm_min + (float)timeinfo->tm_sec / 60.0f;
                aroma_gauge_set_value(state.vehicle_view_clock_gauge, minute_val);
                aroma_gauge_set_secondary_value(state.vehicle_view_clock_gauge, (float)timeinfo->tm_sec);
                aroma_gauge_set_extra_value(state.vehicle_view_clock_gauge, (float)(timeinfo->tm_hour % 12) * 5.0f + (float)timeinfo->tm_min / 12.0f);
                if (state.vehicle_view_ampm_label)
                {
                    const char *ampm = timeinfo->tm_hour >= 12 ? "PM" : "AM";
                    update_label_safe(state.vehicle_view_ampm_label, ampm);
                }
            }
        }

        process_voice_commands();

    
        aroma_ui_process_events();
        update_vehicle_view();

        aroma_ui_render(state.window);

#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }
}