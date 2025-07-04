// This file is part of the OpenXR Dreadful sample application.
// Author: Cass Everitt

#include <game-activity/GameActivity.cpp>
#include <game-text-input/gametextinput.cpp>
#include <span>

#include "AndroidOut.h"
#include "Renderer.h"
#include "xrh.h"
#include "xrhlinear.h"

using namespace std;
using namespace xrh;

namespace {
struct Xr {
  using RendererPtr = std::shared_ptr<Renderer>;

#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
  Xr(android_app* pApp)
#else
  Xr()
#endif
  {
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
    renderer = make_shared<Renderer>(pApp);
    auto dpy = renderer->getDisplay();
    auto cfg = renderer->getConfig();
    auto ctx = renderer->getContext();
    // instance
    inst = make_instance();
    inst->add_required_extension(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
    if (!inst->create()) {
      aout << "OpenXR instance creation failed, exiting." << endl;
      return;
    }
    inst->set_gfx_binding(dpy, cfg, ctx);
#else
    // instance
    inst = make_instance();
    inst->add_required_extension(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
    if (!inst->create()) {
      aout << "OpenXR instance creation failed, exiting." << endl;
    }
#endif
    // session
    ssn = inst->create_session();

    // local space
    auto rsci = RefSpace::element_type::make_create_info();
    local = ssn->create_refspace(rsci);

    auto vcv = inst->get_xr_view_config_view(0);
    auto scci = Swapchain::element_type::make_create_info(vcv.recommendedImageRectWidth, vcv.recommendedImageRectHeight);
    sc = ssn->create_swapchain(scci);

    renderer->setSwapchainImages(sc->get_width(), sc->get_height(), sc->enumerate_images());
  }

  ~Xr() {
    aout << "Destroying Xr instance." << inst.get() << endl;
  }

  RendererPtr get_renderer() {
    return renderer;
  }

  Session get_session() const {
    return ssn;
  }

  Space get_local() const {
    return local;
  }

  bool is_initialized() const {
    return bool(inst);
  }

  bool begin_frame() {
    return ssn->begin_frame();
  }
  Swapchain get_swapchain() const {
    return sc;
  }

  void add_layer(const Layer& layer) {
    ssn->add_layer(layer);
  }

  void end_frame() {
    ssn->end_frame();
  }

 private:
  Instance inst;
  Session ssn;
  Space local;
  Swapchain sc;
  RendererPtr renderer;
};

}  // namespace

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
      aout << "APP_CMD_INIT_WINDOW" << endl;
      pApp->userData = new Xr(pApp);
      break;
    case APP_CMD_TERM_WINDOW:
      // The window is being destroyed. Use this to clean up your userData to avoid leaking
      // resources.
      //
      // We have to check if userData is assigned just in case this comes in really quickly
      aout << "APP_CMD_TERM_WINDOW" << endl;
      if (pApp->userData) {
        //
        auto* pxr = reinterpret_cast<Xr*>(pApp->userData);

        pApp->userData = nullptr;
        delete pxr;
      }
      break;
    default:
      aout << "Unhandled command: " << cmd << endl;
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
    if (!pApp->userData) {
      continue;
    }
    // We know that our user data is a Renderer, so reinterpret cast it. If you change your
    // user data remember to change it here
    auto& xr = *reinterpret_cast<Xr*>(pApp->userData);

    // Process game input
    xr.get_renderer()->handleInput();

    if (!xr.begin_frame()) {
      // We can't begin a frame until the session is in a valid state.
      static int frameCount = 0;
      frameCount++;
      if (frameCount % 60 == 0) {
        aout << "Waiting for session to become synchronized, frame count: " << frameCount << endl;
      }
      continue;
    }

    // Acquire the next image index for the swapchain
    Swapchain sc = xr.get_swapchain();
    if (sc) {
      uint32_t imageIndex = sc->acquire_and_wait_image();

      // Render a frame
      xr.get_renderer()->render(imageIndex);

      // add a layer to be submitted at the end of the frame
      xrh::QuadLayer quad;
      double t = xr.get_session()->get_predicted_display_time() * 1e-9;  // Convert from nanoseconds to seconds

      quad.set_pose(Posef(Quatf(Vector3f(0, 0, 1), t), Vector3f(0, 0, -1)));
      quad.set_size(1.0f, 1.0f);  // Set the size of the quad layer
      quad.set_swapchain(sc);
      quad.set_space(xr.get_local());
      xr.add_layer(quad);

      sc->release_image();
    }

    xr.end_frame();
  } while (!pApp->destroyRequested);
}
}