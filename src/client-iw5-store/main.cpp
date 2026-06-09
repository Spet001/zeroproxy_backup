#include "common.hpp"
#include "logger/logger.hpp"
#include <proxies/d3d9.dll.hpp>
#include <windows.h>
#include <string>
#include "utils/hook.hpp"
#include "component/uwp_hook.hpp"

utils::hook::detour create_file_a_hook;
utils::hook::detour create_file_w_hook;

HANDLE WINAPI create_file_a_stub(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (lpFileName && strstr(lpFileName, ".ff")) {
        LOG("UWP", INFO, "CreateFileA attempting to load: {}", lpFileName);
    }
    auto func = reinterpret_cast<HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE)>(create_file_a_hook.get_original());
    return func(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

HANDLE WINAPI create_file_w_stub(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (lpFileName && wcsstr(lpFileName, L".ff")) {
        std::wstring ws(lpFileName);
        std::string s(ws.begin(), ws.end());
        LOG("UWP", INFO, "CreateFileW attempting to load: {}", s);
    }
    auto func = reinterpret_cast<HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE)>(create_file_w_hook.get_original());
    return func(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

utils::hook::detour get_file_attributes_a_hook;
utils::hook::detour get_file_attributes_w_hook;

DWORD WINAPI get_file_attributes_a_stub(LPCSTR lpFileName) {
    if (lpFileName && strstr(lpFileName, ".ff")) {
        LOG("UWP", INFO, "GetFileAttributesA checking: {}", lpFileName);
    }
    auto func = reinterpret_cast<DWORD(WINAPI*)(LPCSTR)>(get_file_attributes_a_hook.get_original());
    return func(lpFileName);
}

DWORD WINAPI get_file_attributes_w_stub(LPCWSTR lpFileName) {
    if (lpFileName && wcsstr(lpFileName, L".ff")) {
        std::wstring ws(lpFileName);
        std::string s(ws.begin(), ws.end());
        LOG("UWP", INFO, "GetFileAttributesW checking: {}", s);
    }
    auto func = reinterpret_cast<DWORD(WINAPI*)(LPCWSTR)>(get_file_attributes_w_hook.get_original());
    return func(lpFileName);
}

namespace settings {
    void init();
}

namespace xinput_probe {
    void init();
}

DWORD WINAPI init_thread(LPVOID) {
    create_file_a_hook.create(GetProcAddress(GetModuleHandleA("kernelbase.dll"), "CreateFileA"), create_file_a_stub);
    create_file_w_hook.create(GetProcAddress(GetModuleHandleA("kernelbase.dll"), "CreateFileW"), create_file_w_stub);
    get_file_attributes_a_hook.create(GetProcAddress(GetModuleHandleA("kernelbase.dll"), "GetFileAttributesA"), get_file_attributes_a_stub);
    get_file_attributes_w_hook.create(GetProcAddress(GetModuleHandleA("kernelbase.dll"), "GetFileAttributesW"), get_file_attributes_w_stub);
    
    // Initialize controller support
    settings::init();
    xinput_probe::init();

    while (!utils::nt::library("xgameruntime.dll").is_valid()) {
        Sleep(100);
    }
    LOG("UWP", INFO, "Found xgameruntime.dll! Initializing standalone hooks...");
    uwp::init_standalone_hooks();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h_mod, DWORD reason, PVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h_mod);

        // Assign client module for the logger to know where to save ZeroProxy.log
        client_module = h_mod;

        // Initialize d3d9 Proxy natively
        proxy::on_dll_process_attach(h_mod, false);

        // Initialize standalone UWP hooks for DLC Unlock in the background
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)init_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}

namespace proxy {
    void on_direct3d9_created(IDirect3D9* direct3d9) {}
}
