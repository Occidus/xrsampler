#include "xrh.h"

#include <span>

#include "AndroidOut.h"

using namespace std;

XrResult XRH_CheckErrors(XrResult result, const char* function) {
  if (XR_FAILED(result)) {
    char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
    xrResultToString(XR_NULL_HANDLE, result, errorBuffer);
    aout << "OpenXR error: " << function << ": (" << result << ") " << errorBuffer << endl;
  }
  return result;
}
#define XRH(func) XRH_CheckErrors(func, #func);

#define DECL_PFN(pfn) PFN_##pfn pfn = nullptr
#define INIT_PFN(inst, pfn) XRH(xrGetInstanceProcAddr(inst, #pfn, reinterpret_cast<PFN_xrVoidFunction*>(&pfn)))

#define DECL_INIT_PFN(inst, pfn) \
  DECL_PFN(pfn);                 \
  INIT_PFN(inst, pfn)

namespace {
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
DECL_PFN(xrGetOpenGLESGraphicsRequirementsKHR);
#endif

XrExtensionProperties make_ext_prop(const char* name, uint32_t ver) {
  XrExtensionProperties ep{XR_TYPE_EXTENSION_PROPERTIES};
  strcpy(ep.extensionName, name);
  ep.extensionVersion = ver;
  return ep;
}

XrInstanceProperties get_instance_properties(XrInstance inst) {
  XrInstanceProperties ii = {XR_TYPE_INSTANCE_PROPERTIES};
  XRH(xrGetInstanceProperties(inst, &ii));
  aout << "Runtime: " << ii.runtimeName << "Version: " << XR_VERSION_MAJOR(ii.runtimeVersion) << "."
       << XR_VERSION_MINOR(ii.runtimeVersion) << "." << XR_VERSION_PATCH(ii.runtimeVersion) << endl;
  return ii;
}

XrSystemId get_system_id(XrInstance inst) {
  XrSystemGetInfo sysGetInfo = {XR_TYPE_SYSTEM_GET_INFO};
  sysGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

  XrSystemId sysid;
  XrResult res;
  XRH(res = xrGetSystem(inst, &sysGetInfo, &sysid));
  if (res != XR_SUCCESS) {
    aout << "Failed to get system." << endl;
    return XR_NULL_SYSTEM_ID;
  }
  return sysid;
}

XrSystemProperties get_system_properties(XrInstance inst, XrSystemId sysid) {
  XrSystemProperties sysprops = {XR_TYPE_SYSTEM_PROPERTIES};
  XRH(xrGetSystemProperties(inst, sysid, &sysprops));

  aout << "System Properties: Name=" << sysprops.systemName << " VendorId=" << sysprops.vendorId << endl;
  aout << "System Graphics Properties:"
       << " MaxWidth=" << sysprops.graphicsProperties.maxSwapchainImageWidth
       << " MaxHeight=" << sysprops.graphicsProperties.maxSwapchainImageHeight
       << " MaxLayers=" << sysprops.graphicsProperties.maxLayerCount << endl;
  return sysprops;
}

vector<XrExtensionProperties> enumerate_extensions() {
  uint32_t ext_count = 0;
  auto res = xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr);
  if (res != XR_SUCCESS && ext_count == 0) {
    return vector<XrExtensionProperties>();
  }
  vector<XrExtensionProperties> ep(ext_count, {XR_TYPE_EXTENSION_PROPERTIES});
  res = xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, ep.data());
  if (res != XR_SUCCESS) {
    return vector<XrExtensionProperties>();
  }
  return ep;
}

bool ext_supported(span<const XrExtensionProperties> ext_span, const XrExtensionProperties& ext) {
  for (const auto& el : ext_span) {
    if (!strcmp(ext.extensionName, el.extensionName) && ext.extensionVersion <= el.extensionVersion) {
      return true;
    }
  }
  return false;
}

}  // namespace

namespace xrh {
bool init_loader(JavaVM* vm, jobject ctx) {
  DECL_INIT_PFN(XR_NULL_HANDLE, xrInitializeLoaderKHR);
  if (xrInitializeLoaderKHR == nullptr) {
    return false;
  }
  {
    XrLoaderInitInfoAndroidKHR ii{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    ii.applicationVM = vm;
    ii.applicationContext = ctx;
    auto result = xrInitializeLoaderKHR(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&ii));
  }
  return true;
}

Instance::Instance() {}

Instance::~Instance() {}

void Instance::add_required_extension(const char* extName, uint32_t ver) {
  ext.required.push_back(make_ext_prop(extName, ver));
}

void Instance::add_desired_extension(const char* extName, uint32_t ver) {
  ext.desired.push_back(make_ext_prop(extName, ver));
}

bool Instance::create() {
  ext.available = ::enumerate_extensions();
  ext.enabled.clear();
  bool foundRequired = true;
  for (auto& req : ext.required) {
    if (ext_supported(ext.available, req)) {
      ext.enabled.push_back(req);
    } else {
      foundRequired = false;
      aout << "Required extension not supported: " << req.extensionName << "(v" << req.extensionVersion << ")" << endl;
    }
  }
  if (!foundRequired) {
    return false;
  }
  for (auto& des : ext.desired) {
    if (ext_supported(ext.available, des)) {
      ext.enabled.push_back(des);
    } else {
      aout << "Desired extension not supported: " << des.extensionName << "(v" << des.extensionVersion << ")" << endl;
    }
  }
  vector<const char*> extNames;
  for (const auto& en : ext.enabled) {
    extNames.push_back(en.extensionName);
  }

  XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
  ci.enabledExtensionCount = extNames.size();
  ci.enabledExtensionNames = extNames.data();
  inst = XR_NULL_HANDLE;
  auto res = XRH(xrCreateInstance(&ci, &inst));
  if (inst == XR_NULL_HANDLE) {
    aout << "XrInstance creation failed." << endl;
    return false;
  }

  instprops = ::get_instance_properties(inst);
  sysid = ::get_system_id(inst);
  sysprops = ::get_system_properties(inst, sysid);

#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
  DECL_INIT_PFN(inst, xrGetOpenGLESGraphicsRequirementsKHR);
  gfxreqs = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
  XRH(xrGetOpenGLESGraphicsRequirementsKHR(inst, sysid, &gfxreqs));
#endif

  // Get view config info
  uint32_t viewConfigTypeCount = 0;
  XRH(xrEnumerateViewConfigurations(inst, sysid, 0, &viewConfigTypeCount, nullptr));

  vector<XrViewConfigurationType> viewConfigTypes(viewConfigTypeCount);

  XRH(xrEnumerateViewConfigurations(inst, sysid, viewConfigTypeCount, &viewConfigTypeCount, viewConfigTypes.data()));

  aout << "Available Viewport Configuration Types: " << viewConfigTypeCount << endl;

  bool succeeded = false;
  for (const auto& vct : viewConfigTypes) {
    if (vct != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
      continue;
    }
    uint32_t viewCount = 0;
    XRH(xrEnumerateViewConfigurationViews(inst, sysid, vct, 0, &viewCount, nullptr));

    if (viewCount != 2) {
      continue;
    }

    XrViewConfigurationProperties vcp = {XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
    XRH(xrGetViewConfigurationProperties(inst, sysid, vct, &vcp));
    fov_mutable = vcp.fovMutable;
    aout << "fov_mutable=" << fov_mutable << endl;

    for (auto& vcv : view_config_views) {
      vcv = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
    }

    XRH(xrEnumerateViewConfigurationViews(inst, sysid, vct, viewCount, &viewCount, view_config_views.data()));

    {
      auto& e = view_config_views[0];
      aout << "recommended dims: [w=" << e.recommendedImageRectWidth << ", h=" << e.recommendedImageRectHeight
           << ", s=" << e.recommendedSwapchainSampleCount << "]" << endl;
    }
    succeeded = true;
  }

  return succeeded;
}

void Instance::destroy() {
  if (inst == XR_NULL_HANDLE) {
    return;
  }
  XRH(xrDestroyInstance(inst));
  inst = XR_NULL_HANDLE;
}

#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
void Instance::set_gfx_binding(EGLDisplay dpy, EGLConfig cfg, EGLContext ctx) {
  gfxbinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
  gfxbinding.display = dpy;
  gfxbinding.config = cfg;
  gfxbinding.context = ctx;
}
#endif

Session* Instance::create_session() {
  XrSessionCreateInfo ci = {XR_TYPE_SESSION_CREATE_INFO};
  ci.next = &gfxbinding;
  ci.createFlags = 0;
  ci.systemId = sysid;

  XrSession sess = XR_NULL_HANDLE;

  auto res = XRH(xrCreateSession(inst, &ci, &sess));
  if (sess == XR_NULL_HANDLE) {
    aout << "Failed to create XR session" << endl;
    return nullptr;
  }
  return new Session(this, sess);
}

Session::Session(Instance* inst_, XrSession ssn_) : inst(inst_), ssn(ssn_) {
  uint32_t numRefSpaces = 0;
  XRH(xrEnumerateReferenceSpaces(ssn, 0, &numRefSpaces, nullptr));
  vector<XrReferenceSpaceType> refspaces(numRefSpaces);
  XRH(xrEnumerateReferenceSpaces(ssn, refspaces.size(), &numRefSpaces, refspaces.data()));

  for (auto rst : refspaces) {
    refspacetypes.insert(rst);
  }
}

Session::~Session() {
  XRH(xrDestroySession(ssn));
}

Space* Session::create_refspace(const XrReferenceSpaceCreateInfo& createInfo) {
  if (refspacetypes.find(createInfo.referenceSpaceType) == refspacetypes.end()) {
    aout << "Unsupported reference space type." << endl;
    return nullptr;
  }

  XrSpace spacehandle = XR_NULL_HANDLE;
  auto res = XRH(xrCreateReferenceSpace(ssn, &createInfo, &spacehandle));
  if (res != XR_SUCCESS) {
    aout << "Reference space creation failed." << endl;
    return nullptr;
  }
  return new RefSpace(this, spacehandle, createInfo);
}

Swapchain* Session::create_swapchain(const XrSwapchainCreateInfo& createInfo) {
  XrSwapchain sc;
  auto res = XRH(xrCreateSwapchain(ssn, &createInfo, &sc));
  if (res != XR_SUCCESS) {
    aout << "Swapchain creation failed." << endl;
  }
  return new Swapchain(this, sc, createInfo);
}

Space::Space(Session* ssn_, XrSpace space_, Space::Type type_) : ssn(ssn_), space(space_), type(type_) {}

Space::~Space() {
  XRH(xrDestroySpace(space));
}

RefSpace::RefSpace(Session* ssn_, XrSpace space_, const CreateInfo& ci_) : Space(ssn_, space_, Space::Type::Reference), ci(ci_) {}

RefSpace::~RefSpace() {}

Swapchain::Swapchain(Session* ssn_, XrSwapchain swapchain_, const XrSwapchainCreateInfo& ci_)
    : ssn(ssn_), swapchain(swapchain_), ci(ci_) {}

Swapchain::~Swapchain() {
  XRH(xrDestroySwapchain(swapchain));
}

}  // namespace xrh
