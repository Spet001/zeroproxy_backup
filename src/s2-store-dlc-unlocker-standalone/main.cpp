#include "common_core.hpp"
#include <unordered_set>
#include <format>

// Include MinHook if needed directly or via utils
#include "utils/hook.hpp"
#include "utils/nt.hpp"

#include "component/uwp_standalone.hpp"

BOOL APIENTRY DllMain(HMODULE h_mod, DWORD reason, PVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h_mod);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)uwp::init_standalone_hooks, nullptr, 0, nullptr);
	}
	return TRUE;
}
