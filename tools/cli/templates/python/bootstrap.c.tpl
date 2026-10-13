/* {{PROJECT_NAME}} - Android bootstrap for Python apps.
 * App code lives in src/main.py (Chaquopy). This file bridges the
 * NativeActivity thread to it: on startup it imports "main" and calls
 * build_ui(), then drives the standard Aroma event/render loop, and
 * finally calls teardown(). Python errors go to logcat. */
#include <aroma.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

static jobject load_class(JNIEnv *env, jobject activity, const char *name)
{
    jclass act_cls = (*env)->GetObjectClass(env, activity);
    if (!act_cls)
        return NULL;
    jmethodID get_loader = (*env)->GetMethodID(env, act_cls, "getClassLoader",
                                               "()Ljava/lang/ClassLoader;");
    if (!get_loader)
        return NULL;
    jobject loader = (*env)->CallObjectMethod(env, activity, get_loader);
    if (!loader)
        return NULL;
    jclass loader_cls = (*env)->GetObjectClass(env, loader);
    jmethodID load_class = (*env)->GetMethodID(env, loader_cls, "loadClass",
                                               "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!load_class)
        return NULL;
    jstring jname = (*env)->NewStringUTF(env, name);
    jobject cls = (*env)->CallObjectMethod(env, loader, load_class, jname);
    (*env)->DeleteLocalRef(env, jname);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return cls;
}

static bool call_python_hook(struct android_app *app, const char *hook)
{
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
    {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return false;
    }
    jobject activity = app->activity->clazz;

    
    {
        Dl_info info;
        if (dladdr((void *)aroma_ui_init_impl, &info) && info.dli_fname)
            setenv("AROMA_LIB", info.dli_fname, 1);
    }

    
    jobject plat_cls = load_class(env, activity,
                                  "com.chaquo.python.android.AndroidPlatform");
    jobject py_cls = load_class(env, activity, "com.chaquo.python.Python");
    if (!plat_cls || !py_cls)
        return false;
    jmethodID is_started = (*env)->GetStaticMethodID(env, py_cls, "isStarted",
                                                     "()Z");
    if (!is_started)
        return false;
    if (!(*env)->CallStaticBooleanMethod(env, py_cls, is_started))
    {
        jmethodID plat_ctor = (*env)->GetMethodID(env, plat_cls, "<init>",
                                                  "(Landroid/content/Context;)V");
        if (!plat_ctor)
            return false;
        jobject plat = (*env)->NewObject(env, plat_cls, plat_ctor, activity);
        if (!plat)
            return false;
        jmethodID start = (*env)->GetStaticMethodID(env, py_cls, "start",
                                                    "(Lcom/chaquo/python/Python$Platform;)V");
        if (!start)
            return false;
        (*env)->CallStaticVoidMethod(env, py_cls, start, plat);
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
            return false;
        }
    }

    
    jmethodID get_instance = (*env)->GetStaticMethodID(env, py_cls, "getInstance",
                                                       "()Lcom/chaquo/python/Python;");
    if (!get_instance)
        return false;
    jobject py = (*env)->CallStaticObjectMethod(env, py_cls, get_instance);
    if (!py)
        return false;
    jmethodID get_module = (*env)->GetMethodID(env, py_cls, "getModule",
                                               "(Ljava/lang/String;)Lcom/chaquo/python/PyObject;");
    if (!get_module)
        return false;
    jstring mod_name = (*env)->NewStringUTF(env, "main");
    jobject mod = (*env)->CallObjectMethod(env, py, get_module, mod_name);
    (*env)->DeleteLocalRef(env, mod_name);
    if ((*env)->ExceptionCheck(env) || !mod)
    {
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        return false;
    }
    jobject po_cls = load_class(env, activity, "com.chaquo.python.PyObject");
    if (!po_cls)
        return false;
    jmethodID call_attr = (*env)->GetMethodID(env, po_cls, "callAttr",
                                              "(Ljava/lang/String;[Ljava/lang/Object;)Lcom/chaquo/python/PyObject;");
    if (!call_attr)
        return false;
    jclass obj_cls = (*env)->FindClass(env, "java/lang/Object");
    jobject empty = (*env)->NewObjectArray(env, 0, obj_cls, NULL);
    jstring hook_name = (*env)->NewStringUTF(env, hook);
    (*env)->CallObjectMethod(env, mod, call_attr, hook_name, empty);
    bool ok = !(*env)->ExceptionCheck(env);
    if (!ok)
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }
    return ok;
}

void android_main(struct android_app *state)
{
    aroma_android_set_app(state);
    call_python_hook(state, "build_ui");
    while (aroma_ui_is_running_impl())
    {
        aroma_ui_process_events_impl();
        aroma_ui_render_all_windows_impl();
        usleep(16000);
    }
    call_python_hook(state, "teardown");
}
