#include "common.hpp"
#include <loader/component_loader.hpp>

#if defined(_WIN64)
#include "uwp_hook.hpp"

namespace uwp {
    class component final : public generic_component {
    public:
        void post_unpack() override {
            // Initialize DLC Unlocker UWP Hooks after Arxan is unpacked
            init_standalone_hooks();
        }
    };
}

REGISTER_COMPONENT(uwp::component)
#endif


