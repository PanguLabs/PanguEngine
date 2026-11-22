#include <jni.h>

#include <Common_3/OS/Interfaces/IOperatingSystem.h>

// Forge 的 Vulkan Swappy 路径通过此符号借用宿主的 Activity。
WindowDesc gWindow;

jint AndroidAttachToCurrentThread(WindowDesc* window, JNIEnv** env)
{
    if (env == NULL)
    {
        return JNI_EINVAL;
    }
    *env = NULL;
    if (window == NULL || window->handle.activity == NULL || window->handle.activity->vm == NULL)
    {
        return JNI_EINVAL;
    }

    JavaVM* const vm = window->handle.activity->vm;
    return (*vm)->AttachCurrentThread(vm, env, NULL);
}
