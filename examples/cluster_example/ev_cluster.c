#include <unistd.h>
#include <pthread.h>
#include <aroma.h>
#include "aroma_incense_loader.h"
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include "telemetry_frame.h"
#define UI_FILE "../ui.aroma"
#define FRAME_SLEEP_US 16667

#define FONT_SIZE_BODY 18
#define FONT_SIZE_HERO 80

static pthread_t uart_thread;
static pthread_mutex_t telemetry_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int uart_thread_running = 0;
static volatile int new_telemetry_available = 0;
static int serial_fd = -1;

static AromaNode *speed_label = NULL;
static AromaNode *battery_label = NULL;
static AromaNode *range_label = NULL;
static AromaNode *temp_label = NULL;
static AromaNode *clock_label = NULL;
static AromaNode *gear_active_card = NULL;
static AromaNode *range_bar = NULL;
static AromaNode *low_battery_container = NULL;
static AromaNode *gps_lat_label = NULL;
static AromaNode *gps_lon_label = NULL;
static AromaNode *satellites_label = NULL;
static AromaNode *accel_label = NULL;
static AromaNode *throttle_bar = NULL;
static AromaNode *humidity_label = NULL;
static AromaNode *pressure_label = NULL;
static AromaNode *cpu_load_label = NULL;

static int serial_open(const char *port)
{
    int fd = open(port, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror("open");
        return -1;
    }

    struct termios tty;

    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0)
    {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcflush(fd, TCIFLUSH);

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

static int read_exact(int fd, void *buffer, size_t size)
{
    uint8_t *p = buffer;
    size_t total = 0;

    while (total < size)
    {
        ssize_t n = read(fd, p + total, size - total);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (n == 0)
            continue;

        total += n;
    }

    return 0;
}

static void fetch_widget_pointers(IncenseRegistry *registry)
{
    if (!registry)
    {
        fprintf(stderr, "Registry is NULL, cannot fetch widget pointers\n");
        return;
    }

    speed_label = IncenseFindWidget(registry, "speed_value");
    battery_label = IncenseFindWidget(registry, "range_value");
    range_label = IncenseFindWidget(registry, "battery_percent");
    temp_label = IncenseFindWidget(registry, "outside_temp");
    clock_label = IncenseFindWidget(registry, "clock_value");
    gear_active_card = IncenseFindWidget(registry, "gear_active");
    range_bar = IncenseFindWidget(registry, "range_bar");
    gps_lat_label = IncenseFindWidget(registry, "gps_lat_value");
    gps_lon_label = IncenseFindWidget(registry, "gps_lon_value");
    satellites_label = IncenseFindWidget(registry, "gps_sat_value");
    accel_label = IncenseFindWidget(registry, "accel_value");
    humidity_label = IncenseFindWidget(registry, "humidity_display");
    pressure_label = IncenseFindWidget(registry, "pressure_value");
    cpu_load_label = IncenseFindWidget(registry, "cpu_load_value");

    printf("Widget pointers fetched:\n");
    if (speed_label) printf("  speed_value (%p)\n", (void*)speed_label);
    else printf("  speed_value not found\n");
    
    if (battery_label) printf("  range_value (%p)\n", (void*)battery_label);
    else printf("  range_value not found\n");
    
    if (range_label) printf("  battery_percent (%p)\n", (void*)range_label);
    else printf("  battery_percent not found\n");
    
    if (temp_label) printf("  outside_temp (%p)\n", (void*)temp_label);
    else printf("  outside_temp not found\n");
    
    if (clock_label) printf("  clock_value (%p)\n", (void*)clock_label);
    else printf("  clock_value not found\n");
    
    if (gear_active_card) printf("  gear_active (%p)\n", (void*)gear_active_card);
    else printf("  gear_active not found\n");
    
    if (range_bar) printf("  range_bar (%p)\n", (void*)range_bar);
    else printf("  range_bar not found\n");
    
    if (gps_lat_label) printf("  gps_lat_value (%p)\n", (void*)gps_lat_label);
    else printf("  gps_lat_value not found\n");
    
    if (gps_lon_label) printf("  gps_lon_value (%p)\n", (void*)gps_lon_label);
    else printf("  gps_lon_value not found\n");
    
    if (satellites_label) printf("  gps_sat_value (%p)\n", (void*)satellites_label);
    else printf("  gps_sat_value not found\n");
    
    if (accel_label) printf("  accel_value (%p)\n", (void*)accel_label);
    else printf("  accel_value not found\n");
    
    if (humidity_label) printf("  humidity_display (%p)\n", (void*)humidity_label);
    else printf("  humidity_display not found\n");
    
    if (pressure_label) printf("  pressure_value (%p)\n", (void*)pressure_label);
    else printf("  pressure_value not found\n");
    
    if (cpu_load_label) printf("  cpu_load_value (%p)\n", (void*)cpu_load_label);
    else printf("  cpu_load_value not found\n");
}

static void *uart_reader_thread(void *arg)
{
    (void)arg;
    sdv_telemetry_t local_telemetry;
    char text_buffer[64];

    printf("UART reader thread started\n");

    while (uart_thread_running)
    {
        if (read_exact(serial_fd, &local_telemetry, sizeof(local_telemetry)) == 0)
        {
            if (local_telemetry.magic == 0xA5)
            {
                pthread_mutex_lock(&telemetry_mutex);
                
                if (speed_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%d", 
                             (int)(local_telemetry.veh_speed_x10 / 10.0f));
                    aroma_label_set_text(speed_label, text_buffer);
                }
                
                if (accel_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%.2f m/s", 
                             local_telemetry.veh_accel_x100 / 100.0f);
                    aroma_label_set_text(accel_label, text_buffer);
                }
                
                if (temp_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%.1fC", 
                             local_telemetry.env_temp_x10 / 10.0f);
                    aroma_label_set_text(temp_label, text_buffer);
                }
                
                if (humidity_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%.1f%%", 
                             local_telemetry.env_hum_x100 / 100.0f);
                    aroma_label_set_text(humidity_label, text_buffer);
                }
                
                if (pressure_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%.1f hPa", 
                             local_telemetry.env_press_pa / 100.0f);
                    aroma_label_set_text(pressure_label, text_buffer);
                }
                
                if (gps_lat_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%.6f", 
                             local_telemetry.gps_lat_x1e6 / 1000000.0);
                    aroma_label_set_text(gps_lat_label, text_buffer);
                }
                
                if (gps_lon_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%.6f", 
                             local_telemetry.gps_lon_x1e6 / 1000000.0);
                    aroma_label_set_text(gps_lon_label, text_buffer);
                }
                
                if (satellites_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%u", 
                             local_telemetry.gps_satellites);
                    aroma_label_set_text(satellites_label, text_buffer);
                }
                
                if (cpu_load_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%.1f%%", 
                             local_telemetry.cpu_load_x100 / 100.0f);
                    aroma_label_set_text(cpu_load_label, text_buffer);
                }
                
                if (battery_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%.1f", 
                             local_telemetry.cpu_load_x100 / 100.0f);
                    aroma_label_set_text(battery_label, text_buffer);
                }
                
                if (range_label)
                {
                    snprintf(text_buffer, sizeof(text_buffer), "%.0f km", 
                             (local_telemetry.gps_lat_x1e6 / 1000000.0f) * 100 + 200);
                    aroma_label_set_text(range_label, text_buffer);
                }
                
                if (range_bar)
                {
                    float battery_val = local_telemetry.cpu_load_x100 / 100.0f;
                    aroma_progressbar_set_progress(range_bar, battery_val);
                }
                
                if (clock_label)
                {
                    int hours = (local_telemetry.seq / 3600) % 24;
                    int minutes = (local_telemetry.seq / 60) % 60;
                    snprintf(text_buffer, sizeof(text_buffer), "%02d:%02d", hours, minutes);
                    aroma_label_set_text(clock_label, text_buffer);
                }
                
                new_telemetry_available = 1;
                pthread_mutex_unlock(&telemetry_mutex);
            }
        }
        else
        {
            usleep(1000);
        }
    }

    printf("UART reader thread stopped\n");
    return NULL;
}

static void set_gear_park(void *userdata) {
    (void)userdata;
}

static void set_gear_reverse(void *userdata) {
    (void)userdata;
}

static void set_gear_neutral(void *userdata) {
    (void)userdata;
}

static void set_gear_drive(void *userdata) {
    (void)userdata;
}

int main(void)
{
    set_minimum_log_level(DEBUG_LEVEL_WARNING);

    aroma_ui_init();

    AromaFont *font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                    aroma_ubuntu_ttf_len,
                                                    FONT_SIZE_BODY);
    AromaFont *icon_font = aroma_font_create_from_memory(icon_ttf,
                                                         icon_ttf_len,
                                                         FONT_SIZE_BODY);
    AromaFont *big_font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                        aroma_ubuntu_ttf_len,
                                                        FONT_SIZE_HERO);
    AromaFont *medium_font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                        aroma_ubuntu_ttf_len,
                                                        50);
    if (!font || !icon_font || !big_font || !medium_font)
    {
        aroma_ui_shutdown();
        return 1;
    }

    IncenseRegisterFont("big_font", big_font);
    IncenseRegisterFont("medium_font", medium_font);

    IncenseRegisterCallback("set_gear_park", INCENSE_CALLBACK_VOID_PTR, set_gear_park, NULL);
    IncenseRegisterCallback("set_gear_reverse", INCENSE_CALLBACK_VOID_PTR, set_gear_reverse, NULL);
    IncenseRegisterCallback("set_gear_neutral", INCENSE_CALLBACK_VOID_PTR, set_gear_neutral, NULL);
    IncenseRegisterCallback("set_gear_drive", INCENSE_CALLBACK_VOID_PTR, set_gear_drive, NULL);

    AromaTheme theme = aroma_theme_create_material_black();
    theme.colors.primary = 0xFF2196F3;
    aroma_ui_set_theme(&theme);
    IncenseRegistry *registry = NULL;
    
    int watcher = IncenseHotReloadStart(UI_FILE, font, icon_font, &registry);
    fetch_widget_pointers(registry);

    if (watcher < 0)
    {
        aroma_font_destroy(font);
        aroma_font_destroy(icon_font);
        aroma_font_destroy(big_font);
        aroma_ui_shutdown();
        return 1;
    }

    IncenseHotReloadSetCallback(watcher, NULL);

    AromaWindow *window = IncenseHotReloadGetWindow(watcher);

    if (!window)
    {
        aroma_font_destroy(font);
        aroma_font_destroy(icon_font);
        aroma_font_destroy(big_font);
        aroma_ui_shutdown();
        return 1;
    }

    aroma_event_set_root((AromaNode *)window);


    serial_fd = serial_open("/dev/ttyACM0");

    if (serial_fd < 0)
    {
        fprintf(stderr, "Failed to open UART\n");
    }
    else
    {
        uart_thread_running = 1;
        if (pthread_create(&uart_thread, NULL, uart_reader_thread, NULL) != 0)
        {
            fprintf(stderr, "Failed to create UART thread\n");
            uart_thread_running = 0;
            close(serial_fd);
            serial_fd = -1;
        }
    }

    while (aroma_ui_is_running())
    {
        if (new_telemetry_available)
        {
            pthread_mutex_lock(&telemetry_mutex);
            new_telemetry_available = 0;
            pthread_mutex_unlock(&telemetry_mutex);
        }

        int reloaded = IncenseHotReloadCheck();

        if (reloaded > 0)
        {
            AromaWindow *new_window = IncenseHotReloadGetWindow(watcher);

            if (new_window && new_window != window)
            {
                window = new_window;
                aroma_event_set_root((AromaNode *)window);
            }
        }

        aroma_ui_process_events();
        aroma_ui_render(window);

        usleep(FRAME_SLEEP_US);
    }

    if (uart_thread_running)
    {
        uart_thread_running = 0;
        pthread_join(uart_thread, NULL);
    }

    if (serial_fd >= 0)
    {
        close(serial_fd);
    }

    pthread_mutex_destroy(&telemetry_mutex);

    IncenseHotReloadStopAll();
    aroma_ui_destroy_window(window);

    aroma_font_destroy(big_font);
    aroma_font_destroy(icon_font);
    aroma_font_destroy(font);
    aroma_font_destroy(medium_font);

    aroma_ui_shutdown();
    return 0;
}