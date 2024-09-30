
#include <SDL2/SDL_main.h>
#include <SDL2/SDL_vulkan.h>
#include <rpp/base.h>
#include <rpp/thread.h>

#include "diopter.h"
#include "platform/window.h"

using namespace rpp;

int main(int, char**) {

    Thread::set_priority(Thread::Priority::high);

    {
        Profile::Time_Point start = Profile::timestamp();
        info("Setting up window...");
        Window window;

        info("Setting up rvk...");

        Vec<String_View, rvk::Alloc> extensions;

        Region(R) {
            Vec<const char*, Mregion<R>> sdl_extensions;
            u32 n_extensions = 0;
            if(!SDL_Vulkan_GetInstanceExtensions(window.sdl(), &n_extensions, null)) {
                die("Failed to get SDL Vulkan extensions: %", String_View{SDL_GetError()});
            }
            sdl_extensions.resize(n_extensions);
            if(!SDL_Vulkan_GetInstanceExtensions(window.sdl(), &n_extensions,
                                                 sdl_extensions.data())) {
                die("Failed to get SDL Vulkan extensions: %", String_View{SDL_GetError()});
            }
            extensions.reserve(n_extensions);
            for(auto& ext : sdl_extensions) {
                extensions.push(String_View{ext});
            }
        }

#if RPP_DEBUG_BUILD
        bool validation = true;
        bool robust_accesses = true;
#else
        bool validation = false;
        bool robust_accesses = false;
#endif
        rvk::startup(rvk::Config{
            .validation = validation,
            .robust_accesses = robust_accesses,
            .ray_tracing = true,
            .imgui = true,
            .hdr = true,
            .frames_in_flight = 2,
            .descriptors_per_type = 256,
            .layers = Slice<const String_View>{},
            .swapchain_extensions = extensions.slice(),
            .create_surface =
                [&](VkInstance instance) {
                    VkSurfaceKHR surface;
                    if(!SDL_Vulkan_CreateSurface(window.sdl(), instance, &surface)) {
                        die("Failed to create SDL Vulkan surface: %", String_View{SDL_GetError()});
                    }
                    return surface;
                },
            .host_heap = Math::GB(2),
            .device_heap = Math::MB(8188),
        });

        {
            info("Starting diopter...");
            Diopter diopter(window);
            Profile::Time_Point end = Profile::timestamp();
            info("Started up diopter in %ms!", Profile::ms(end - start));
            diopter.loop();
            info("Shutting down diopter...");
        }

        rvk::shutdown();
    }
    info("Shut down diopter.");

    Profile::finalize();

    return 0;
}
