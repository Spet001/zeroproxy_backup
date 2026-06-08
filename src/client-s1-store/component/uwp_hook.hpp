#pragma once
#include "common_core.hpp"
#include <string>
#include <unordered_set>
#include <format>
#include <cstdint>
#include "utils/hook.hpp"
#include "utils/nt.hpp"
#include "logger/logger.hpp"
#include "engine/uwp/IStoreImpl6.hpp"

namespace uwp {
    struct XVersion {
        uint16_t major;
        uint16_t minor;
        uint16_t build;
        uint16_t revision;
    };

    struct XPackageDetails {
        const char* packageIdentifier;
        XVersion    version;
        uint32_t    kind;
        uint8_t     _pad_kind[4];
        const char* displayName;
        const char* description;
        const char* publisher;
        const char* storeId;
        bool        installing;
        uint8_t     _pad0[3];
        uint32_t    index;
        uint32_t    count;
        bool        ageRestricted;
        uint8_t     _pad1[3];
        const char* titleID;
    };
    static_assert(sizeof(XPackageDetails) == 80, "XPackageDetails size mismatch");

    inline void* get_vt_function(void* class_ptr, std::size_t index) {
        struct vt_cls { void** vftable_; };
        return PTR_AS(vt_cls*, class_ptr)->vftable_[index];
    }

    inline std::unordered_set<uwp::XAsyncBlock*> fake_async_blocks;

    inline DWORD WINAPI fake_async_thread(LPVOID param) {
        uwp::XAsyncBlock* async = (uwp::XAsyncBlock*)param;
        Sleep(1);
        void(*cb)(uwp::XAsyncBlock*) = *(void(**)(uwp::XAsyncBlock*))((uint8_t*)async + 16);
        if (cb) cb(async);
        return 0;
    }

    namespace x_package {
        inline std::unordered_set<void*> fake_mount_handles;

        inline utils::hook::detour x_package_mount_hook;
        inline utils::hook::detour x_package_mount_with_ui_result_hook;
        inline utils::hook::detour x_package_get_mount_path_size_hook;
        inline utils::hook::detour x_package_get_mount_path_hook;
        inline utils::hook::detour x_package_close_mount_handle_hook;
        inline utils::hook::detour x_package_enumerate_packages_v8_hook;
        inline utils::hook::detour x_package_mount_with_ui_async_hook;

        struct PkgEnumCtx {
            void* orig_cb;
            void* orig_ctx;
        };

        inline bool __stdcall hook_pkg_enum_callback(void* ctx, const XPackageDetails* details) {
            if (!ctx || !details) return true;
            PkgEnumCtx* c = reinterpret_cast<PkgEnumCtx*>(ctx);
            typedef bool(__stdcall* PkgEnumCb_t)(void*, const XPackageDetails*);
            PkgEnumCb_t orig = reinterpret_cast<PkgEnumCb_t>(c->orig_cb);
            return orig(c->orig_ctx, details);
        }

        inline HRESULT x_package_enumerate_packages_v8_stub(void* _this, uint32_t kind, uint32_t scope, void* ctx, void* cb) {
            auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, uint32_t, void*, void*)>(x_package_enumerate_packages_v8_hook.get_original());
            PkgEnumCtx wrapper_ctx = { cb, ctx };
            void* call_cb = cb ? (void*)&hook_pkg_enum_callback : nullptr;
            void* call_ctx = cb ? (void*)&wrapper_ctx : ctx;
            return func(_this, kind, scope, call_ctx, call_cb);
        }

        inline HRESULT x_package_mount_stub(void* _this, const char* package_identifier, void** out_handle) {
            if (out_handle) {
                void* fake = malloc(0x10);
                memset(fake, 0, 0x10);
                *out_handle = fake;
                fake_mount_handles.insert(fake);
            }
            return S_OK;
        }

        inline HRESULT x_package_mount_with_ui_async_stub(void* _this, const char* package_identifier, uwp::XAsyncBlock* async) {
            fake_async_blocks.insert(async);
            HANDLE h = CreateThread(nullptr, 0, fake_async_thread, async, 0, nullptr);
            if (h) CloseHandle(h);
            return S_OK;
        }

        inline HRESULT x_package_mount_with_ui_result_stub(void* _this, uwp::XAsyncBlock* async, void** out_handle) {
            if (fake_async_blocks.contains(async)) {
                fake_async_blocks.erase(async);
                if (out_handle) {
                    void* fake = malloc(0x10);
                    memset(fake, 0, 0x10);
                    *out_handle = fake;
                    fake_mount_handles.insert(fake);
                }
                return S_OK;
            }
            auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, void**)>(x_package_mount_with_ui_result_hook.get_original());
            return func(_this, async, out_handle);
        }

        inline HRESULT x_package_get_mount_path_size_stub(void* _this, void* handle, uint64_t* out_size) {
            if (fake_mount_handles.contains(handle)) {
                if (out_size) {
                    char path[MAX_PATH];
                    GetModuleFileNameA(nullptr, path, MAX_PATH);
                    std::string dir(path);
                    auto pos = dir.find_last_of("\\/");
                    dir = (pos != std::string::npos) ? dir.substr(0, pos) : dir;
                    
                    std::string dlc_dir = dir + "\\zone\\dlc\\";
                    if (GetFileAttributesA(dlc_dir.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        dir = dlc_dir;
                    } else {
                        dir += "\\"; // Add trailing slash for safe concatenation
                    }
                    *out_size = dir.size() + 1;
                }
                return S_OK;
            }
            auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, uint64_t*)>(x_package_get_mount_path_size_hook.get_original());
            return func(_this, handle, out_size);
        }

        inline HRESULT x_package_get_mount_path_stub(void* _this, void* handle, uint64_t out_size, char* out_buf) {
            if (fake_mount_handles.contains(handle)) {
                if (out_buf) {
                    char path[MAX_PATH];
                    GetModuleFileNameA(nullptr, path, MAX_PATH);
                    std::string dir(path);
                    auto pos = dir.find_last_of("\\/");
                    dir = (pos != std::string::npos) ? dir.substr(0, pos) : dir;

                    std::string dlc_dir = dir + "\\zone\\dlc\\";
                    if (GetFileAttributesA(dlc_dir.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        dir = dlc_dir;
                    } else {
                        dir += "\\"; // Add trailing slash for safe concatenation
                    }
                    strncpy_s(out_buf, out_size, dir.c_str(), _TRUNCATE);
                }
                return S_OK;
            }
            auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, uint64_t, char*)>(x_package_get_mount_path_hook.get_original());
            return func(_this, handle, out_size, out_buf);
        }

        inline void x_package_close_mount_handle_stub(void* _this, void* handle) {
            if (fake_mount_handles.contains(handle)) {
                fake_mount_handles.erase(handle);
                free(handle);
            }
        }
    }

    namespace x_store {
        inline utils::hook::detour x_store_enumerate_products_query_hook;
        inline utils::hook::detour x_store_query_entitled_products_async_hook;
        inline utils::hook::detour x_store_acquire_license_for_package_async_hook;
        inline utils::hook::detour x_store_acquire_license_for_package_result_hook;
        inline utils::hook::detour x_store_license_is_valid_hook;
        inline utils::hook::detour x_store_close_license_handle_hook;
        inline utils::hook::detour x_store_query_game_license_result_hook;
        inline utils::hook::detour x_store_acquire_license_for_durables_async_hook;
        inline utils::hook::detour x_store_acquire_license_for_durables_result_hook;

        inline HRESULT x_store_acquire_license_for_durables_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, const char* store_id, uwp::XAsyncBlock* async) {
            auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, const char*, void*)>(x_store_acquire_license_for_durables_async_hook.get_original());
            HRESULT hr = func(_this, store_context_handle, store_id, async);
            if (FAILED(hr)) {
                fake_async_blocks.insert(async);
                HANDLE h = CreateThread(nullptr, 0, fake_async_thread, async, 0, nullptr);
                if (h) CloseHandle(h);
                return S_OK;
            }
            return hr;
        }

        inline HRESULT x_store_acquire_license_for_durables_result_stub(uwp::IStoreImpl1* _this, uwp::XAsyncBlock* async, void** out_license) {
            if (fake_async_blocks.contains(async)) {
                fake_async_blocks.erase(async);
                if (out_license) {
                    void* fake = malloc(0x30);
                    memset(fake, 0, 0x30);
                    ((uint8_t*)fake)[0x18] = 1; // OFF_LICENSE_VALID
                    *out_license = fake;
                }
                return S_OK;
            }
            auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, void**)>(x_store_acquire_license_for_durables_result_hook.get_original());
            HRESULT hr = func(_this, async, out_license);
            if (SUCCEEDED(hr) && out_license && *out_license) {
                ((uint8_t*)*out_license)[0x18] = 1; // Force license valid
            } else if (FAILED(hr) && out_license) {
                void* fake = malloc(0x30);
                memset(fake, 0, 0x30);
                ((uint8_t*)fake)[0x18] = 1; // OFF_LICENSE_VALID
                *out_license = fake;
                hr = S_OK;
            }
            return hr;
        }

        inline HRESULT x_store_acquire_license_for_package_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, const char* package_identifier, uwp::XAsyncBlock* async) {
            // FAKE FOR ALL PACKAGES! The async function returns S_OK synchronously even if it fails in the background.
            // By completely intercepting this, we guarantee the game receives a valid fake license handle!
            fake_async_blocks.insert(async);
            HANDLE h = CreateThread(nullptr, 0, fake_async_thread, async, 0, nullptr);
            if (h) CloseHandle(h);
            return S_OK;
        }

        struct ProductsCbCtx {
            bool(*userCb)(const uwp::XStoreProduct*, void*) = nullptr;
            void* userCtx = nullptr;
        };

        inline void PatchProductSkuData(uint8_t* product) {
            uint32_t skuCount = *(uint32_t*)(product + 160);
            uint8_t* skuArr   = *(uint8_t**)(product + 168);
            if (!skuArr || skuCount == 0) return;
            for (uint32_t i = 0; i < skuCount && i < 64; i++) {
                uint8_t* sku = skuArr + (size_t)i * 0x110;
                sku[121]  = 1;
                sku[120]  = 0;
                *(int64_t*)(sku + 128)  = 133484064000000000LL;
                *(int64_t*)(sku + 136)  = 133484064000000000LL;
                *(int64_t*)(sku + 144)  = 0x7FFFFFFFFFFFFFFELL;
                sku[152]                 = 0;
                *(uint32_t*)(sku + 156) = 0;
                *(uint32_t*)(sku + 160)  = 1;
            }
        }

        inline bool hook_get_products_callback(const uwp::XStoreProduct* product, void* ctx) {
            auto* p = const_cast<uwp::XStoreProduct*>(product);
            p->has_digital_download_ = true;
            p->is_in_user_collection_ = true;
            uint8_t* prodBytes = reinterpret_cast<uint8_t*>(p);
            uint32_t skuCount = *(uint32_t*)(prodBytes + 0xA0);
            uint8_t* skuArr = *(uint8_t**)(prodBytes + 0xA8);
            if (skuArr && skuCount > 0) {
                for (uint32_t i = 0; i < skuCount && i < 64; i++) {
                    uint8_t* sku = skuArr + (size_t)i * 0x110;
                    sku[121]  = 1;
                    sku[120]  = 0;
                    *(int64_t*)(sku + 128)  = 133484064000000000LL;
                    *(int64_t*)(sku + 136)  = 133484064000000000LL;
                    *(int64_t*)(sku + 144)  = 0x7FFFFFFFFFFFFFFELL;
                    sku[152]                 = 0;
                    *(uint32_t*)(sku + 156)  = 0;
                    *(uint32_t*)(sku + 160)  = 1;
                }
            }
            auto* c = reinterpret_cast<ProductsCbCtx*>(ctx);
            return c->userCb ? c->userCb(product, c->userCtx) : true;
        }

        inline HRESULT x_store_enumerate_products_query_stub(uwp::IStoreImpl1* _this, const uwp::XStoreProductQueryHandle product_query_handle, void* context, bool(*callback)(const uwp::XStoreProduct*, void*)) {
            ProductsCbCtx cbCtx;
            cbCtx.userCb = callback;
            cbCtx.userCtx = context;
            void* callCtx = callback ? (void*)&cbCtx : context;
            void* callCb  = callback ? (void*)&hook_get_products_callback : nullptr;

            HRESULT hr = x_store_enumerate_products_query_hook.invoke<HRESULT>(_this, product_query_handle, callCtx, callCb);

            if (!callback && hr == S_OK && product_query_handle) {
                auto* base = *(uint8_t**)((uint8_t*)product_query_handle + 24);
                auto* end  = *(uint8_t**)((uint8_t*)product_query_handle + 32);
                for (uint8_t* p = base; base && p < end; p += 208) {
                    if (!p[145]) { p[145] = 1; }
                    if (!p[144]) { p[144] = 1; }
                    PatchProductSkuData(p);
                }
            }
            return hr;
        }

        inline HRESULT x_store_query_entitled_products_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, uwp::XStoreProductKind product_kinds, std::uint32_t max_items_to_retrieve_per_page, uwp::XAsyncBlock* async) {
            auto func = get_vt_function(_this, 5); // QueryAssociatedProductsAsync
            typedef HRESULT(__stdcall* QueryAssociatedT)(uwp::IStoreImpl1*, uwp::XStoreContextHandle, uwp::XStoreProductKind, std::uint32_t, uwp::XAsyncBlock*);
            return ((QueryAssociatedT)func)(_this, store_context_handle, product_kinds, max_items_to_retrieve_per_page, async);
        }

        inline HRESULT x_store_acquire_license_for_package_result_stub(uwp::IStoreImpl1* _this, uwp::XAsyncBlock* async, void** out_license) {
            if (fake_async_blocks.contains(async)) {
                fake_async_blocks.erase(async);
                if (out_license) {
                    void* fake = malloc(0x30);
                    memset(fake, 0, 0x30);
                    ((uint8_t*)fake)[0x18] = 1;
                    *out_license = fake;
                }
                return S_OK;
            }
            auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, void**)>(x_store_acquire_license_for_package_result_hook.get_original());
            HRESULT hr = func(_this, async, out_license);
            if (SUCCEEDED(hr) && out_license && *out_license) {
                ((uint8_t*)*out_license)[0x18] = 1;
            } else if (FAILED(hr) && out_license) {
                void* fake = malloc(0x30);
                memset(fake, 0, 0x30);
                ((uint8_t*)fake)[0x18] = 1;
                *out_license = fake;
                hr = S_OK;
            }
            return hr;
        }

        inline HRESULT x_store_license_is_valid_stub(void* _this, void* license) {
            if (license) {
                uint8_t* ptr = reinterpret_cast<uint8_t*>(license);
                if (ptr[0x18] == 1) { return 1; }
            }
            auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(x_store_license_is_valid_hook.get_original());
            return func(_this, license);
        }

        inline void x_store_close_license_handle_stub(uwp::IStoreImpl1* _this, void* handle) {
            if (handle) { free(handle); }
        }

        inline HRESULT x_store_query_game_license_result_stub(uwp::IStoreImpl1* _this, uwp::XAsyncBlock* async, uwp::XStoreGameLicense* license) {
            x_store_query_game_license_result_hook.invoke<HRESULT>(_this, async, license);
            license->is_active_ = true;
            license->is_disc_license_ = false;
            license->is_trial_ = false;
            return S_OK;
        }
    }

    inline void init_standalone_hooks() {
        utils::nt::library lib("xgameruntime.dll");
        if (!lib.is_valid()) return;

        auto query_api_impl = lib.get_proc<HRESULT(__stdcall*)(GUID*, GUID*, void**)>("QueryApiImpl");
        if (!query_api_impl) return;

        // Fetch XPackage Interface
        void* pkg_api = nullptr;
        GUID pkg_first =  { 0xaf406016, 0xe850, 0x4aa8, { 0xa8, 0x8d, 0x2f, 0x3d, 0xcb, 0x9d, 0xac, 0x7e } };
        GUID pkg_second = { 0xe2a4734b, 0x2f4a, 0x456d, { 0xaa, 0x8f, 0xd0, 0x65, 0xe0, 0x4f, 0xb2, 0x09 } };
        
        if (SUCCEEDED(query_api_impl(&pkg_first, &pkg_second, &pkg_api)) && pkg_api) {
            x_package::x_package_enumerate_packages_v8_hook.create(get_vt_function(pkg_api, 39), x_package::x_package_enumerate_packages_v8_stub);
            x_package::x_package_mount_hook.create(get_vt_function(pkg_api, 36), x_package::x_package_mount_stub);
            x_package::x_package_mount_with_ui_async_hook.create(get_vt_function(pkg_api, 37), x_package::x_package_mount_with_ui_async_stub);
            x_package::x_package_mount_with_ui_result_hook.create(get_vt_function(pkg_api, 38), x_package::x_package_mount_with_ui_result_stub);
            x_package::x_package_get_mount_path_size_hook.create(get_vt_function(pkg_api, 24), x_package::x_package_get_mount_path_size_stub);
            x_package::x_package_get_mount_path_hook.create(get_vt_function(pkg_api, 25), x_package::x_package_get_mount_path_stub);
            x_package::x_package_close_mount_handle_hook.create(get_vt_function(pkg_api, 26), x_package::x_package_close_mount_handle_stub);
        }

        // Fetch XStore Interface
        void* store_api = nullptr;
        GUID store_first =  { 0x0dd112ac, 0x7c24, 0x448c, { 0xb9, 0x2b, 0x39, 0x60, 0xfb, 0x5b, 0xd3, 0x0c } };
        GUID store_second = { 0x5c48dedf, 0x0b67, 0x4492, { 0xa4, 0xb5, 0x68, 0x29, 0xb8, 0xe7, 0x96, 0xe1 } };

        if (SUCCEEDED(query_api_impl(&store_first, &store_second, &store_api)) && store_api) {
            x_store::x_store_query_entitled_products_async_hook.create(get_vt_function(store_api, 9), x_store::x_store_query_entitled_products_async_stub);
            x_store::x_store_enumerate_products_query_hook.create(get_vt_function(store_api, 15), x_store::x_store_enumerate_products_query_stub);
            x_store::x_store_acquire_license_for_package_async_hook.create(get_vt_function(store_api, 20), x_store::x_store_acquire_license_for_package_async_stub);
            x_store::x_store_acquire_license_for_package_result_hook.create(get_vt_function(store_api, 21), x_store::x_store_acquire_license_for_package_result_stub);
            x_store::x_store_license_is_valid_hook.create(get_vt_function(store_api, 22), x_store::x_store_license_is_valid_stub);
            x_store::x_store_close_license_handle_hook.create(get_vt_function(store_api, 23), x_store::x_store_close_license_handle_stub);
            x_store::x_store_query_game_license_result_hook.create(get_vt_function(store_api, 29), x_store::x_store_query_game_license_result_stub);
            x_store::x_store_acquire_license_for_durables_async_hook.create(get_vt_function(store_api, 30), x_store::x_store_acquire_license_for_durables_async_stub);
            x_store::x_store_acquire_license_for_durables_result_hook.create(get_vt_function(store_api, 31), x_store::x_store_acquire_license_for_durables_result_stub);
        }
    }
}
