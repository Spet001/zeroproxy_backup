#include "common.hpp"
#include "logger/logger.hpp"
#include <proxies/d3d9.dll.hpp>
#include "component/uwp_hook.hpp"

BOOL APIENTRY DllMain(HMODULE h_mod, DWORD reason, PVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h_mod);

        // Assign client module for the logger to know where to save ZeroProxy.log
        client_module = h_mod;

        // Initialize d3d9 Proxy natively
        proxy::on_dll_process_attach(h_mod, false);

        // Initialize standalone UWP hooks for DLC Unlock in the background
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)uwp::init_standalone_hooks, nullptr, 0, nullptr);
    }
    return TRUE;
}

namespace proxy {
    void on_direct3d9_created(IDirect3D9* direct3d9) {}
}
