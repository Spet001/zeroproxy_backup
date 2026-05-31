#include "common.hpp"

#include <loader/component_loader.hpp>
#include <utils/hook.hpp>

namespace unpack_state {
	struct component final : generic_component {
		void post_load() override {
			// Do nothing for MS Store (unpacked binary)
		}
	};
}

REGISTER_COMPONENT(unpack_state::component)