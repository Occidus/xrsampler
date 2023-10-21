#include <jni.h>

#include "AndroidOut.h"
#include "Renderer.h"

#include <game-activity/GameActivity.cpp>
#include <game-text-input/gametextinput.cpp>

#if defined(ANDROID)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_PLATFORM_ANDROID 1
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

extern "C"
{

#include <game-activity/native_app_glue/android_native_app_glue.c>

    /*!
     * Handles commands sent to this Android application
     * @param pApp the app the commands are coming from
     * @param cmd the command to handle
     */
    void handle_cmd(android_app *pApp, int32_t cmd)
    {
        switch (cmd)
        {
        case APP_CMD_INIT_WINDOW:
            // A new window is created, associate a renderer with it. You may replace this with a
            // "game" class if that suits your needs. Remember to change all instances of userData
            // if you change the class here as a reinterpret_cast is dangerous this in the
            // android_main function and the APP_CMD_TERM_WINDOW handler case.
            pApp->userData = new Renderer(pApp);
            break;
        case APP_CMD_TERM_WINDOW:
            // The window is being destroyed. Use this to clean up your userData to avoid leaking
            // resources.
            //
            // We have to check if userData is assigned just in case this comes in really quickly
            if (pApp->userData)
            {
                //
                auto *pRenderer = reinterpret_cast<Renderer *>(pApp->userData);
                pApp->userData = nullptr;
                delete pRenderer;
            }
            break;
        default:
            break;
        }
    }

    /*!
     * Enable the motion events you want to handle; not handled events are
     * passed back to OS for further processing. For this example case,
     * only pointer and joystick devices are enabled.
     *
     * @param motionEvent the newly arrived GameActivityMotionEvent.
     * @return true if the event is from a pointer or joystick device,
     *         false for all other input devices.
     */
    bool motion_event_filter_func(const GameActivityMotionEvent *motionEvent)
    {
        auto sourceClass = motionEvent->source & AINPUT_SOURCE_CLASS_MASK;
        return (sourceClass == AINPUT_SOURCE_CLASS_POINTER ||
                sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK);
    }

    // TODO: Move this to a separate OpenXR source file.
    bool initOpenXR(JavaVM *vm, jobject ctx)
    {
        PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
        {
            auto result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", reinterpret_cast<PFN_xrVoidFunction *>(&xrInitializeLoaderKHR));
            if (result != XR_SUCCESS)
            {
                aout << "xrGetInstanceProcAddr(xrInitializeLoaderKHR) failed" << std::endl;
                // Log error message
                return false;
            }
            aout << "got here: " << __FUNCTION__ << ":" << __LINE__ << std::endl;
        }
        {
            XrLoaderInitInfoAndroidKHR ii{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
            ii.applicationVM = vm;
            ii.applicationContext = ctx;
            auto result = xrInitializeLoaderKHR(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR *>(&ii));
        }
        // until we complete the implementation of this function,
        // just return false...
        aout << "finish implementing this function" << std::endl;
        return false;
        // return true;
    }

    /*!
     * This the main entry point for a native activity
     */
    void android_main(struct android_app *pApp)
    {
        // Can be removed, useful to ensure your code is running
        aout << "Welcome to android_main" << std::endl;

        // Register an event handler for Android events
        pApp->onAppCmd = handle_cmd;

        if (!initOpenXR(pApp->activity->vm, pApp->activity->javaGameActivity))
        {
            aout << "OpenXR initialization failed, exiting";
            return;
        }

        // Set input event filters (set it to NULL if the app wants to process all inputs).
        // Note that for key inputs, this example uses the default default_key_filter()
        // implemented in android_native_app_glue.c.
        android_app_set_motion_event_filter(pApp, motion_event_filter_func);

        // This sets up a typical game/event loop. It will run until the app is destroyed.
        int events;
        android_poll_source *pSource;
        do
        {
            // Process all pending events before running game logic.
            if (ALooper_pollAll(0, nullptr, &events, (void **)&pSource) >= 0)
            {
                if (pSource)
                {
                    pSource->process(pApp, pSource);
                }
            }

            // Check if any user data is associated. This is assigned in handle_cmd
            if (pApp->userData)
            {

                // We know that our user data is a Renderer, so reinterpret cast it. If you change your
                // user data remember to change it here
                auto *pRenderer = reinterpret_cast<Renderer *>(pApp->userData);

                // Process game input
                pRenderer->handleInput();

                // Render a frame
                pRenderer->render();
            }
        } while (!pApp->destroyRequested);
    }
}