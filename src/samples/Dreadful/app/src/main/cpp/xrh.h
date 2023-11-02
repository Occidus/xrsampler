#pragma once

#if defined(ANDROID)
#include <jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_PLATFORM_ANDROID 1
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <vector>
namespace xrh
{
#if defined(ANDROID)
    // must be called before constructor
    bool init_loader(JavaVM *vm, jobject ctx);
#endif
    class instance
    {
    public:
        void add_required_extension(const char *name, uint32_t ver = 1);
        void add_desired_extension(const char *name, uint32_t ver = 1);

        std::vector<XrExtensionProperties> get_available_extensions() const
        {
            return ext.available;
        }

        bool create();

        // only available after successful create:

        XrInstance get_instance() const
        {
            return inst;
        }

        XrInstanceProperties get_instance_properties() const
        {
            return instprops;
        }

        XrSystemId get_system_id() const
        {
            return sysid;
        }

        XrSystemProperties get_system_properties() const
        {
            return sysprops;
        }

        std::vector<XrExtensionProperties> get_enabled_extensions() const
        {
            return ext.enabled;
        }

    private:
        struct extensions
        {
            std::vector<XrExtensionProperties> available;
            std::vector<XrExtensionProperties> required;
            std::vector<XrExtensionProperties> desired;
            std::vector<XrExtensionProperties> enabled;
        };

        extensions ext;
        XrInstance inst;
        XrInstanceProperties instprops;
        XrSystemId sysid;
        XrSystemProperties sysprops;
    };

}