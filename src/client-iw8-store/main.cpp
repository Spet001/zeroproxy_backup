#include "common.hpp"
#include <windows.h>
#include <proxies/d3d11.dll.hpp>
#include "component/network_isolation.hpp"
#include "component/uwp_hook.hpp"

BOOL APIENTRY DllMain(HMODULE h_mod, DWORD reason, PVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h_mod);

        // Setup the proxy so the game actually loads this as d3d11.dll
        proxy::on_dll_process_attach(h_mod, false);

        // Initialize the network isolation unconditionally, not working at the moment and needs to be reworked, but it should be fine to have it running in the background for now
      //  network::init_isolation_hooks();

        // Initialize UWP hooks to bypass DRM checks
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)uwp::init_standalone_hooks, nullptr, 0, nullptr);
	}
	return TRUE;
}
