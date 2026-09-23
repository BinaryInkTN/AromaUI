#include <unistd.h>

#include "main_loop.h"
#include "app_state.h"
#include "voice_handler.h"
#include "aroma.h"
#include "aroma_animation.h"
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

static void update_digital_clock(void)
{
    if (!state.vehicle_view_clock_label && !state.vehicle_view_ampm_label &&
        !state.vehicle_view_clock_date_label)
        return;
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    if (!timeinfo)
        return;
    int hour12 = timeinfo->tm_hour % 12;
    if (hour12 == 0)
        hour12 = 12;
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%d:%02d", hour12, timeinfo->tm_min);
    update_label_safe(state.vehicle_view_clock_label, time_buf);
    update_label_safe(state.vehicle_view_ampm_label,
                      timeinfo->tm_hour >= 12 ? "PM" : "AM");
    if (state.vehicle_view_clock_label && state.vehicle_view_ampm_label)
    {
        AromaRect *tr = aroma_node_get_rect(state.vehicle_view_clock_label);
        AromaRect *ar = aroma_node_get_rect(state.vehicle_view_ampm_label);
        if (tr && ar)
        {
            int total = tr->width + 12 + ar->width;
            int tx = WIN_W / 2 - total / 2;
            if (tx != tr->x)
            {
                tr->x = tx;
                aroma_node_invalidate(state.vehicle_view_clock_label);
            }
            int ax = tx + tr->width + 12;
            if (ax != ar->x)
            {
                ar->x = ax;
                aroma_node_invalidate(state.vehicle_view_ampm_label);
            }
        }
    }
    if (state.vehicle_view_clock_date_label)
    {
        char date_buf[64];
        strftime(date_buf, sizeof(date_buf), "%a, %b %d", timeinfo);
        update_label_safe(state.vehicle_view_clock_date_label, date_buf);
        AromaRect *dr = aroma_node_get_rect(state.vehicle_view_clock_date_label);
        if (dr)
        {
            int dx = WIN_W / 2 - dr->width / 2;
            if (dx != dr->x)
            {
                dr->x = dx;
                aroma_node_invalidate(state.vehicle_view_clock_date_label);
            }
        }
    }
}

void main_loop()
{

    uint64_t last_time_update = aroma_time_now_ms();
    (void)last_time_update;

    update_digital_clock();

    while (aroma_ui_is_running())
    {

        uint64_t now = aroma_time_now_ms();
        (void)now;

        update_digital_clock();

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
