#pragma once
#include "common.hpp"
#include "game/function_types.hpp"

namespace game {
	void init();

	inline functions::SEH_StringEd_GetStringT* SEH_StringEd_GetString{};

	bool is_server();

	std::uintptr_t get_base();

	inline std::uintptr_t relocate(const std::uintptr_t val) {
		if (!val) return 0;
		return get_base() + val;
	}

	inline std::uintptr_t derelocate(const std::uintptr_t val) {
		if (!val) return 0;
		return val - get_base();
	}

	inline std::uintptr_t derelocate(const void* val) {
		return derelocate(reinterpret_cast<std::uintptr_t>(val));
	}

	namespace environment {
		bool is_sp();
		bool is_mp();
	}
}

inline std::uintptr_t operator"" _g(const unsigned long long val) {
	return game::relocate(static_cast<std::uintptr_t>(val));
}
