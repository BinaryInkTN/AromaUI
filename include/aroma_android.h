#ifndef AROMA_ANDROID_H
#define AROMA_ANDROID_H

#ifdef __ANDROID__

#include <jni.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
JNIEnv* aroma_android_get_env();
jobject aroma_android_get_activity();
JavaVM* aroma_android_get_jvm();
bool aroma_android_check_permission(const char* permission_name);
void aroma_android_request_permission(const char* permission_name);
void aroma_android_toast(const char* msg, bool long_duration);
void aroma_android_open_settings();
void aroma_android_vibrate(int ms);
int aroma_android_get_battery_level();
bool aroma_android_is_wifi_enabled();
void aroma_android_set_wifi_enabled(bool enabled); 
bool aroma_android_is_bluetooth_enabled(); 
void aroma_android_launch_camera(); 
void aroma_android_launch_gallery();
jobject aroma_android_get_system_service(const char* service_name);
#ifdef __cplusplus
}
#endif

#endif 
#endif 
