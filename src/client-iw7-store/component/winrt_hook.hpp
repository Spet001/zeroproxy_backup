#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <roapi.h>
#include <winstring.h>
#pragma comment(lib, "runtimeobject.lib")
#include <utils/nt.hpp>
#include <utils/hook.hpp>
#include <logger/logger.hpp>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace component::winrt_hook {
    
    typedef HRESULT(__stdcall* RoGetActivationFactory_t)(HSTRING, REFIID, void**);
    typedef HRESULT(__stdcall* RoActivateInstance_t)(HSTRING, IInspectable**);
    typedef HRESULT(__stdcall* QueryInterface_t)(IUnknown*, REFIID, void**);
    
    utils::hook::detour ro_get_activation_factory_hook;
    utils::hook::detour ro_activate_instance_hook;
    utils::hook::detour query_interface_hook;
    
    std::vector<void*> store_instances;
    std::recursive_mutex store_instances_mutex;
    
    std::unordered_map<void*, void**> original_vtables; // Maps object instance to its original vtable array
    
    std::string hstring_to_string(HSTRING hstr) {
        if (!hstr) return "null";
        const wchar_t* wstr = WindowsGetStringRawBuffer(hstr, nullptr);
        if (!wstr) return "null";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
        if (size_needed <= 0 || size_needed > 10000) return "invalid_size";
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], size_needed, NULL, NULL);
        return str.c_str();
    }
    
    typedef HRESULT(__stdcall* GenericMethod_t)(void*, void*, void*, void*);
    
    template<int Index>
    HRESULT __stdcall method_stub(void* a1, void* a2, void* a3, void* a4) {
        LOG("WinRT", INFO, "IStoreContextServer::Method[{}] called!", Index);
        
        void** orig_vtable = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(store_instances_mutex);
            if (original_vtables.find(a1) != original_vtables.end()) {
                orig_vtable = original_vtables[a1];
            }
        }
        
        if (!orig_vtable) {
            LOG("WinRT", ERROR, "Could not find original vtable for instance %p!", a1);
            return E_FAIL;
        }
        
        auto orig = reinterpret_cast<GenericMethod_t>(orig_vtable[Index]);
        HRESULT hr = orig(a1, a2, a3, a4);
        
        if (hr == S_OK) {
            if (Index == 11 || Index == 12) {
                if (a2) {
                    void* op = *(void**)a2;
                    if (op) LOG("WinRT", INFO, "  -> a2 returned operation: %p", op);
                }
            } else if (Index == 13 || Index == 14 || Index == 15) {
                if (a3) {
                    void* op = *(void**)a3;
                    if (op) LOG("WinRT", INFO, "  -> a3 returned operation: %p", op);
                }
                if (a4) {
                    void* op = *(void**)a4;
                    if (op) LOG("WinRT", INFO, "  -> a4 returned operation: %p", op);
                }
            }
        }
        
        return hr;
    }
    
    void hook_vtable_stealth(void* instance) {
        std::lock_guard<std::recursive_mutex> lock(store_instances_mutex);
        if (original_vtables.find(instance) != original_vtables.end()) return; 
        
        void** original_vtable = *(void***)instance;
        original_vtables[instance] = original_vtable;
        
        LOG("WinRT", INFO, "Applying STEALTH VTable Hook on instance %p (VTable %p)", instance, (void*)original_vtable);
        
        // We only need to hook up to index 16. So we need at least 17 pointers.
        int vtable_size = 17;
        void** fake_vtable = new void*[vtable_size];
        
        // Safely copy exactly 17 pointers
        for (int i = 0; i < vtable_size; ++i) {
            fake_vtable[i] = original_vtable[i];
        }
        
        // Replace pointers in the fake vtable
        if (fake_vtable[6]) fake_vtable[6] = method_stub<6>;
        if (fake_vtable[7]) fake_vtable[7] = method_stub<7>;
        if (fake_vtable[8]) fake_vtable[8] = method_stub<8>;
        if (fake_vtable[9]) fake_vtable[9] = method_stub<9>;
        if (fake_vtable[10]) fake_vtable[10] = method_stub<10>;
        if (fake_vtable[11]) fake_vtable[11] = method_stub<11>;
        if (fake_vtable[12]) fake_vtable[12] = method_stub<12>;
        if (fake_vtable[13]) fake_vtable[13] = method_stub<13>;
        if (fake_vtable[14]) fake_vtable[14] = method_stub<14>;
        if (fake_vtable[15]) fake_vtable[15] = method_stub<15>;
        if (fake_vtable[16]) fake_vtable[16] = method_stub<16>;
        
        // Overwrite the object's vtable pointer
        DWORD oldProtect;
        VirtualProtect(instance, sizeof(void*), PAGE_READWRITE, &oldProtect);
        *(void***)instance = fake_vtable;
        VirtualProtect(instance, sizeof(void*), oldProtect, &oldProtect);
        
        LOG("WinRT", INFO, "Stealth VTable Hook applied successfully!");
    }
    
    HRESULT __stdcall query_interface_stub(IUnknown* _this, REFIID riid, void** ppvObject) {
        bool is_store = false;
        {
            std::lock_guard<std::recursive_mutex> lock(store_instances_mutex);
            for (auto inst : store_instances) {
                if (inst == _this) {
                    is_store = true;
                    break;
                }
            }
        }
        
        std::string tmp_iid;
        if (is_store) {
            LPOLESTR iid_str = nullptr;
            StringFromIID(riid, &iid_str);
            if (iid_str) {
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, iid_str, -1, NULL, 0, NULL, NULL);
                tmp_iid = std::string(size_needed, 0);
                WideCharToMultiByte(CP_UTF8, 0, iid_str, -1, &tmp_iid[0], size_needed, NULL, NULL);
                CoTaskMemFree(iid_str);
            }
        }
        
        auto orig = reinterpret_cast<QueryInterface_t>(query_interface_hook.get_original());
        HRESULT result = orig(_this, riid, ppvObject);
        
        if (is_store && result == S_OK && ppvObject && *ppvObject) {
            std::lock_guard<std::recursive_mutex> lock(store_instances_mutex);
            store_instances.push_back(*ppvObject);
            
            if (tmp_iid.find("A561605D") != std::string::npos || tmp_iid.find("a561605d") != std::string::npos || tmp_iid.find("565333BC") != std::string::npos || tmp_iid.find("565333bc") != std::string::npos) {
                hook_vtable_stealth(*ppvObject);
            }
        }
        
        return result;
    }
    
    HRESULT __stdcall ro_get_activation_factory_stub(HSTRING activatableClassId, REFIID iid, void** factory) {
        auto orig = reinterpret_cast<RoGetActivationFactory_t>(ro_get_activation_factory_hook.get_original());
        return orig(activatableClassId, iid, factory);
    }
    
    HRESULT __stdcall ro_activate_instance_stub(HSTRING activatableClassId, IInspectable** instance) {
        std::string class_id = hstring_to_string(activatableClassId);
        auto orig = reinterpret_cast<RoActivateInstance_t>(ro_activate_instance_hook.get_original());
        HRESULT result = orig(activatableClassId, instance);
        
        if (class_id.find("StoreContextServer") != std::string::npos) {
            LOG("WinRT", INFO, "RoActivateInstance requested: {}", class_id);
            if (result == S_OK && instance && *instance) {
                {
                    std::lock_guard<std::recursive_mutex> lock(store_instances_mutex);
                    store_instances.push_back(*instance);
                }
                void** vtable = *(void***)(*instance);
                void* original_qi = vtable[0];
                if (query_interface_hook.get_original() == nullptr) {
                    query_interface_hook.create(original_qi, query_interface_stub);
                    query_interface_hook.enable();
                }
            }
        }
        return result;
    }

    void init() {
        HMODULE combase_mod = GetModuleHandleA("combase.dll");
        if (!combase_mod) return;
        
        void* ro_activate_instance_addr = GetProcAddress(combase_mod, "RoActivateInstance");
        if (ro_activate_instance_addr) {
            ro_activate_instance_hook.create(ro_activate_instance_addr, ro_activate_instance_stub);
            ro_activate_instance_hook.enable();
        }
    }
}

