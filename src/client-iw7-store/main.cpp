#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <logger/logger.hpp>
#include "component/winrt_hook.hpp"

#include <common_core.hpp>

void init() {
    Sleep(100);
    LOG("EntryPoint", INFO, "ZeroProxy for IW7-Store successfully injected!");
    
    // Initialize WinRT Phase 1 Hooks
    component::winrt_hook::init();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        client_module = hModule;
        DisableThreadLibraryCalls(hModule);
        
        // Use CreateThread to avoid deadlocks in DllMain
        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            init();
            return 0;
        }, nullptr, 0, nullptr);
        
        if (hThread) {
            CloseHandle(hThread);
        }
    }
    return TRUE;
}
