#include <game-activity/GameActivity.cpp>
#include <game-text-input/gametextinput.cpp>

#include "AndroidOut.h"
#include "Renderer.h"
#include "xrh.h"
#include "xrhlinear.h"

using namespace std;
using namespace xrh;

extern "C" {

#include <game-activity/native_app_glue/android_native_app_glue.c>

/*!
 * Handles commands sent to this Android application
 * @param pApp the app the commands are coming from
 * @param cmd the command to handle
 */
void handle_cmd(android_app* pApp, int32_t cmd) {
  switch (cmd) {
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
      if (pApp->userData) {
        //
        auto* pRenderer = reinterpret_cast<Renderer*>(pApp->userData);
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
bool motion_event_filter_func(const GameActivityMotionEvent* motionEvent) {
  auto sourceClass = motionEvent->source & AINPUT_SOURCE_CLASS_MASK;
  return (sourceClass == AINPUT_SOURCE_CLASS_POINTER || sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK);
}

namespace {
struct Xr {
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
  bool init(EGLDisplay dpy, EGLConfig cfg, EGLContext ctx)
#else
  bool init()
#endif
  {
    // instance
    inst = new Instance();
    inst->add_required_extension(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
    if (!inst->create()) {
      aout << "OpenXR instance creation failed, exiting." << endl;
      return false;
    }
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
    inst->set_gfx_binding(dpy, cfg, ctx);
#endif
    // session
    ssn = inst->create_session();

    // local space
    auto rsci = RefSpace::make_create_info();
    local = ssn->create_refspace(rsci);

    // swapchain
    auto vcv = inst->get_view_config_view(0);
    auto scci = Swapchain::make_create_info(vcv.recommendedImageRectWidth, vcv.recommendedImageRectHeight);
    sc = ssn->create_swapchain(scci);
    return true;
  }

  void destroy() {
    delete sc;
    delete local;
    delete ssn;
    delete inst;
  }

  Instance* inst = nullptr;
  Session* ssn = nullptr;
  Space* local = nullptr;
  Swapchain* sc = nullptr;
};

}  // namespace

/*!
 * This the main entry point for a native activity
 */
void android_main(struct android_app* pApp) {
  // Can be removed, useful to ensure your code is running
  aout << "Welcome to android_main" << endl;

  // Register an event handler for Android events
  pApp->onAppCmd = handle_cmd;

  if (!init_loader(pApp->activity->vm, pApp->activity->javaGameActivity)) {
    aout << "OpenXR loader initialization failed, exiting" << endl;
    return;
  }

  Xr xr;

  // This sets up a typical game/event loop. It will run until the app is destroyed.
  int events;
  android_poll_source* pSource;
  do {
    // Process all pending events before running game logic.
    if (ALooper_pollAll(0, nullptr, &events, (void**)&pSource) >= 0) {
      if (pSource) {
        pSource->process(pApp, pSource);
      }
    }

    // Check if any user data is associated. This is assigned in handle_cmd
    if (pApp->userData) {
      // We know that our user data is a Renderer, so reinterpret cast it. If you change your
      // user data remember to change it here
      auto* pRenderer = reinterpret_cast<Renderer*>(pApp->userData);

      if (xr.inst == nullptr) {
        xr.init(pRenderer->getDisplay(), pRenderer->getConfig(), pRenderer->getContext());
      }

      // Process game input
      pRenderer->handleInput();

      // Render a frame
      pRenderer->render();
    }
  } while (!pApp->destroyRequested);
  xr.destroy();
}
}