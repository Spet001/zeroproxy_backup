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
        
        typedef HRESULT(__stdcall* XAsyncComplete_t)(uwp::XAsyncBlock* asyncBlock, HRESULT result, size_t requiredBufferSize);
        static XAsyncComplete_t pXAsyncComplete = nullptr;
        if (!pXAsyncComplete) {
            HMODULE hXgame = GetModuleHandleA("xgameruntime.dll");
            if (hXgame) {
                pXAsyncComplete = (XAsyncComplete_t)GetProcAddress(hXgame, "XAsyncComplete");
            }
        }
        
        if (pXAsyncComplete) {
            pXAsyncComplete(async, S_OK, 0);
        } else {
            // Fallback just in case
            void(*cb)(uwp::XAsyncBlock*) = *(void(**)(uwp::XAsyncBlock*))((uint8_t*)async + 16);
            if (cb) cb(async);
        }
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
            
            if (details && details->storeId && details->packageIdentifier) {
                LOG("UWP", INFO, "Intercepted real package enum! ID: {}, StoreID: {}, Kind: {}", details->packageIdentifier, details->storeId, details->kind);
                
                // Pass the real package as Content (1)
                XPackageDetails fake_details = *details;
                fake_details.kind = 1;
                orig(c->orig_ctx, &fake_details);
                
                // Pass the real package as Durable (2)
                fake_details.kind = 2;
                orig(c->orig_ctx, &fake_details);

                // Pass the real package as Game (3)
                fake_details.kind = 3;
                return orig(c->orig_ctx, &fake_details);
            }
            return orig(c->orig_ctx, details);
        }

        inline HRESULT x_package_enumerate_packages_v8_stub(void* _this, uint32_t kind, uint32_t scope, void* ctx, void* cb) {
            LOG("UWP", INFO, "XPackageEnumeratePackages called with kind: {}, scope: {}", kind, scope);
            auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, uint32_t, void*, void*)>(x_package_enumerate_packages_v8_hook.get_original());
            
            PkgEnumCtx custom_ctx;
            custom_ctx.orig_ctx = ctx;
            custom_ctx.orig_cb = cb;

            HRESULT res = func(_this, kind, scope, &custom_ctx, hook_pkg_enum_callback);

            if (cb) {
                typedef bool(__stdcall* PkgEnumCb_t)(void*, const XPackageDetails*);
                PkgEnumCb_t orig_cb = reinterpret_cast<PkgEnumCb_t>(cb);
                
                XPackageDetails fake_details = {};
                fake_details.packageIdentifier = "ModernWarfare3.DLC.Mock";
                fake_details.storeId = "9PM614FZFSP1"; // Use the real MW3 DLC StoreID!
                fake_details.displayName = "Modern Warfare 3 DLC";

                // Inject as Content
                fake_details.kind = 1;
                orig_cb(ctx, &fake_details);

                // Inject as Durable
                fake_details.kind = 2;
                orig_cb(ctx, &fake_details);

                LOG("UWP", INFO, "Injected fake DLC package with StoreID 9PM614FZFSP1 into XPackageEnumeratePackages!");
            }

            return res;
        }

        inline HRESULT x_package_mount_stub(void* _this, const char* package_identifier, void** out_handle) {
            LOG("UWP", INFO, "XPackageMount called for package: {}", package_identifier ? package_identifier : "null");
            if (out_handle) {
                void* fake = malloc(0x10);
                memset(fake, 0, 0x10);
                *out_handle = fake;
                fake_mount_handles.insert(fake);
                LOG("UWP", INFO, "Faked XPackageMount! Injected fake mount handle.");
            }
            return S_OK;
        }

        inline HRESULT x_package_mount_with_ui_async_stub(void* _this, const char* package_identifier, uwp::XAsyncBlock* async) {
            LOG("UWP", INFO, "XPackageMountWithUiAsync called for package: {}", package_identifier ? package_identifier : "null");
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
                    LOG("UWP", INFO, "Faked XPackageMountWithUiResult! Injected fake mount handle.");
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
                    LOG("UWP", INFO, "Faked XPackageGetMountPathSize to: {}", *out_size);
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
                    LOG("UWP", INFO, "Faked XPackageGetMountPath to: {}", out_buf);
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
        
        inline utils::hook::detour x_store_query_products_async_hook;
        inline utils::hook::detour x_store_query_associated_products_async_hook;
        inline utils::hook::detour x_store_query_product_for_current_game_async_hook;
        inline utils::hook::detour x_store_query_product_for_package_async_hook;

        inline HRESULT x_store_query_product_for_current_game_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, uwp::XAsyncBlock* async) {
            LOG("UWP", INFO, "XStoreQueryProductForCurrentGameAsync called!");
            auto func = reinterpret_cast<HRESULT(__stdcall*)(uwp::IStoreImpl1*, const uwp::XStoreContextHandle, uwp::XAsyncBlock*)>(x_store_query_product_for_current_game_async_hook.get_original());
            return func(_this, store_context_handle, async);
        }

        inline HRESULT x_store_query_product_for_package_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, uwp::XStoreProductKind product_kinds, const char* package_identifier, uwp::XAsyncBlock* async) {
            uint32_t kinds = (uint32_t)product_kinds;
            LOG("UWP", INFO, "XStoreQueryProductForPackageAsync called for package: {} with kinds: {}", package_identifier ? package_identifier : "null", kinds);
            auto func = reinterpret_cast<HRESULT(__stdcall*)(uwp::IStoreImpl1*, const uwp::XStoreContextHandle, uwp::XStoreProductKind, const char*, uwp::XAsyncBlock*)>(x_store_query_product_for_package_async_hook.get_original());
            return func(_this, store_context_handle, product_kinds, package_identifier, async);
        }

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
            LOG("UWP", INFO, "XStoreAcquireLicenseForPackageAsync called! Package: {}", package_identifier);
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
            LOG("UWP", INFO, "XStoreProduct patched! title={} storeId={}", p->title_ ? p->title_ : "null", p->store_id_ ? p->store_id_ : "null");
            return c->userCb ? c->userCb(product, c->userCtx) : true;
        }

        inline HRESULT x_store_enumerate_products_query_stub(uwp::IStoreImpl1* _this, const uwp::XStoreProductQueryHandle product_query_handle, void* context, bool(*callback)(const uwp::XStoreProduct*, void*)) {
            LOG("UWP", INFO, "XStoreEnumerateProductsQuery called!");
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
            uint32_t kinds = (uint32_t)product_kinds;
            LOG("UWP", INFO, "XStoreQueryEntitledProductsAsync called with product_kinds: {}", kinds);
            auto func = get_vt_function(_this, 5); // QueryAssociatedProductsAsync
            typedef HRESULT(__stdcall* QueryAssociatedT)(uwp::IStoreImpl1*, uwp::XStoreContextHandle, uwp::XStoreProductKind, std::uint32_t, uwp::XAsyncBlock*);
            LOG("UWP", INFO, "Intercepted XStoreQueryEntitledProductsAsync! Redirecting to XStoreQueryAssociatedProductsAsync...");
            return ((QueryAssociatedT)func)(_this, store_context_handle, product_kinds, max_items_to_retrieve_per_page, async);
        }

        inline HRESULT x_store_query_products_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, uwp::XStoreProductKind product_kinds, const char** store_ids, std::uint32_t store_ids_count, const char** action_filters, std::uint32_t action_filters_count, uwp::XAsyncBlock* async) {
            uint32_t kinds = (uint32_t)product_kinds;
            LOG("UWP", INFO, "XStoreQueryProductsAsync called with product_kinds: {}, store_ids_count: {}", kinds, store_ids_count);
            for (uint32_t i = 0; i < store_ids_count; ++i) {
                LOG("UWP", INFO, "  store_id[{}]: {}", i, store_ids[i] ? store_ids[i] : "null");
            }
            auto func = reinterpret_cast<HRESULT(__stdcall*)(uwp::IStoreImpl1*, const uwp::XStoreContextHandle, uwp::XStoreProductKind, const char**, std::uint32_t, const char**, std::uint32_t, uwp::XAsyncBlock*)>(x_store_query_products_async_hook.get_original());
            return func(_this, store_context_handle, product_kinds, store_ids, store_ids_count, action_filters, action_filters_count, async);
        }

        inline HRESULT x_store_query_associated_products_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, uwp::XStoreProductKind product_kinds, std::uint32_t max_items_to_retrieve_per_page, uwp::XAsyncBlock* async) {
            uint32_t kinds = (uint32_t)product_kinds;
            LOG("UWP", INFO, "XStoreQueryAssociatedProductsAsync called with product_kinds: {}", kinds);
            auto func = reinterpret_cast<HRESULT(__stdcall*)(uwp::IStoreImpl1*, const uwp::XStoreContextHandle, uwp::XStoreProductKind, std::uint32_t, uwp::XAsyncBlock*)>(x_store_query_associated_products_async_hook.get_original());
            return func(_this, store_context_handle, product_kinds, max_items_to_retrieve_per_page, async);
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

        utils::hook::detour x_store_query_add_on_licenses_async_hook;
        utils::hook::detour x_store_query_add_on_licenses_result_count_hook;
        utils::hook::detour x_store_query_add_on_licenses_result_hook;

        inline HRESULT x_store_query_add_on_licenses_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, uwp::XAsyncBlock* async) {
            LOG("UWP", INFO, "XStoreQueryAddOnLicensesAsync called! Faking completion...");
            fake_async_blocks.insert(async);
            HANDLE h = CreateThread(nullptr, 0, fake_async_thread, async, 0, nullptr);
            if (h) CloseHandle(h);
            return S_OK;
        }

        inline HRESULT x_store_query_add_on_licenses_result_count_stub(uwp::IStoreImpl1* _this, uwp::XAsyncBlock* async, std::uint32_t* count) {
            LOG("UWP", INFO, "XStoreQueryAddOnLicensesResultCount called!");
            if (fake_async_blocks.contains(async)) {
                if (count) *count = 1; // Fake 1 add-on
                LOG("UWP", INFO, "XStoreQueryAddOnLicensesResultCount faked! Returning count: {}", *count);
                return S_OK;
            }
            auto func = reinterpret_cast<HRESULT(__stdcall*)(uwp::IStoreImpl1*, uwp::XAsyncBlock*, std::uint32_t*)>(x_store_query_add_on_licenses_result_count_hook.get_original());
            return func(_this, async, count);
        }

        inline HRESULT x_store_query_add_on_licenses_result_stub(uwp::IStoreImpl1* _this, uwp::XAsyncBlock* async, std::uint32_t count, uwp::XStoreAddonLicense* addon_licenses) {
            LOG("UWP", INFO, "XStoreQueryAddOnLicensesResult called! Count: {}", count);
            if (fake_async_blocks.contains(async)) {
                fake_async_blocks.erase(async);
                if (addon_licenses && count > 0) {
                    memset(addon_licenses, 0, sizeof(uwp::XStoreAddonLicense) * count);
                    // Fake the MW3 DLC Add-On License
                    strncpy_s(addon_licenses[0].sku_store_id_, "9PM614FZFSP1", _TRUNCATE);
                    addon_licenses[0].is_active_ = true;
                    addon_licenses[0].expiration_date_ = 0x7FFFFFFFFFFFFFFF; // Never expires
                    LOG("UWP", INFO, "Faked XStoreAddonLicense with StoreID 9PM614FZFSP1!");
                }
                return S_OK;
            }
            auto func = reinterpret_cast<HRESULT(__stdcall*)(uwp::IStoreImpl1*, uwp::XAsyncBlock*, std::uint32_t, uwp::XStoreAddonLicense*)>(x_store_query_add_on_licenses_result_hook.get_original());
            return func(_this, async, count, addon_licenses);
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

        utils::hook::detour x_store_query_game_license_async_hook;

        inline HRESULT x_store_query_game_license_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, uwp::XAsyncBlock* async) {
            LOG("UWP", INFO, "XStoreQueryGameLicenseAsync called! Faking completion...");
            fake_async_blocks.insert(async);
            HANDLE h = CreateThread(nullptr, 0, fake_async_thread, async, 0, nullptr);
            if (h) CloseHandle(h);
            return S_OK;
        }

        inline HRESULT x_store_query_game_license_result_stub(uwp::IStoreImpl1* _this, uwp::XAsyncBlock* async, uwp::XStoreGameLicense* license) {
            LOG("UWP", INFO, "XStoreQueryGameLicenseResult called!");
            auto func = reinterpret_cast<HRESULT(__stdcall*)(uwp::IStoreImpl1*, uwp::XAsyncBlock*, uwp::XStoreGameLicense*)>(x_store_query_game_license_result_hook.get_original());
            func(_this, async, license);
            
            if (license) {
                license->is_active_ = true;
                license->is_disc_license_ = false;
                license->is_trial_ = false;
            }
            return S_OK;
        }
    }

    inline utils::hook::detour query_api_impl_hook;

    inline std::string guid_to_string(GUID guid) {
        return std::format("{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }

    inline bool is_target_api(GUID guid_first, GUID guid_second, const std::string& guid_first_target, const std::string& guid_second_target) {
        return guid_to_string(guid_first) == guid_first_target && guid_to_string(guid_second) == guid_second_target;
    }

    inline HRESULT query_api_impl_stub(GUID* first, GUID* second, void** api_out) {
        auto res = query_api_impl_hook.invoke<HRESULT>(first, second, api_out);

        if (first != nullptr && second != nullptr && res >= 0 && api_out && *api_out) {
            static bool hooked_pkg_vt = false;
            static bool hooked_store_vt = false;

            if (!hooked_pkg_vt && is_target_api(*first, *second, "af406016-e850-4aa8-a88d-2f3dcb9dac7e", "e2a4734b-2f4a-456d-aa8f-d065e04fb209")) {
                LOG("UWP", INFO, "Game requested XPackage API! Hooking XPackage interfaces...");
                x_package::x_package_enumerate_packages_v8_hook.create(get_vt_function(*api_out, 39), x_package::x_package_enumerate_packages_v8_stub);
                x_package::x_package_mount_hook.create(get_vt_function(*api_out, 36), x_package::x_package_mount_stub);
                x_package::x_package_mount_with_ui_async_hook.create(get_vt_function(*api_out, 37), x_package::x_package_mount_with_ui_async_stub);
                x_package::x_package_mount_with_ui_result_hook.create(get_vt_function(*api_out, 38), x_package::x_package_mount_with_ui_result_stub);
                x_package::x_package_get_mount_path_size_hook.create(get_vt_function(*api_out, 24), x_package::x_package_get_mount_path_size_stub);
                x_package::x_package_get_mount_path_hook.create(get_vt_function(*api_out, 25), x_package::x_package_get_mount_path_stub);
                x_package::x_package_close_mount_handle_hook.create(get_vt_function(*api_out, 26), x_package::x_package_close_mount_handle_stub);
                hooked_pkg_vt = true;
            }

            if (!hooked_store_vt && is_target_api(*first, *second, "0dd112ac-7c24-448c-b92b-3960fb5bd30c", "5c48dedf-0b67-4492-a4b5-6829b8e796e1")) {
                LOG("UWP", INFO, "Game requested XStore API! Hooking XStore interfaces...");
                x_store::x_store_query_products_async_hook.create(get_vt_function(*api_out, 7), x_store::x_store_query_products_async_stub);
                x_store::x_store_query_associated_products_async_hook.create(get_vt_function(*api_out, 5), x_store::x_store_query_associated_products_async_stub);
                x_store::x_store_query_entitled_products_async_hook.create(get_vt_function(*api_out, 9), x_store::x_store_query_entitled_products_async_stub);
                x_store::x_store_query_product_for_current_game_async_hook.create(get_vt_function(*api_out, 11), x_store::x_store_query_product_for_current_game_async_stub);
                x_store::x_store_query_product_for_package_async_hook.create(get_vt_function(*api_out, 13), x_store::x_store_query_product_for_package_async_stub);
                x_store::x_store_enumerate_products_query_hook.create(get_vt_function(*api_out, 15), x_store::x_store_enumerate_products_query_stub);
                x_store::x_store_acquire_license_for_package_async_hook.create(get_vt_function(*api_out, 20), x_store::x_store_acquire_license_for_package_async_stub);
                x_store::x_store_acquire_license_for_package_result_hook.create(get_vt_function(*api_out, 21), x_store::x_store_acquire_license_for_package_result_stub);
                x_store::x_store_license_is_valid_hook.create(get_vt_function(*api_out, 22), x_store::x_store_license_is_valid_stub);
                x_store::x_store_close_license_handle_hook.create(get_vt_function(*api_out, 23), x_store::x_store_close_license_handle_stub);
                x_store::x_store_query_game_license_result_hook.create(get_vt_function(*api_out, 29), x_store::x_store_query_game_license_result_stub);
                
                x_store::x_store_query_add_on_licenses_async_hook.create(get_vt_function(*api_out, 30), x_store::x_store_query_add_on_licenses_async_stub);
                x_store::x_store_query_add_on_licenses_result_count_hook.create(get_vt_function(*api_out, 31), x_store::x_store_query_add_on_licenses_result_count_stub);
                x_store::x_store_query_add_on_licenses_result_hook.create(get_vt_function(*api_out, 32), x_store::x_store_query_add_on_licenses_result_stub);
                hooked_store_vt = true;
            }
        }
        return res;
    }

    inline void init_standalone_hooks() {
        static bool is_query_api_hooked = false;
        if (is_query_api_hooked) return;

        LOG("UWP", INFO, "Waiting to hook QueryApiImpl...");
        utils::nt::library lib("xgameruntime.dll");
        if (!lib.is_valid()) {
            LOG("UWP", INFO, "Failed to find xgameruntime.dll!");
            return;
        }

        auto query_api_impl = lib.get_proc<FARPROC>("QueryApiImpl");
        if (!query_api_impl) {
            LOG("UWP", INFO, "Failed to find QueryApiImpl!");
            return;
        }

        query_api_impl_hook.create(query_api_impl, query_api_impl_stub);
        is_query_api_hooked = true;
        LOG("UWP", INFO, "Successfully hooked QueryApiImpl!");
    }
}
