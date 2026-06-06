#include <utils/io.hpp>
#pragma once
#include "common_core.hpp"

#if defined(_WIN64)
#include "engine/uwp/IStoreImpl6.hpp"
#include "identification/game.hpp"
#include "loader/component_loader.hpp"
#include "logger/logger.hpp"
#include "utils/hook.hpp"
#include "utils/string.hpp"
#include "component/scheduler.hpp"

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

	namespace {
		utils::hook::detour query_api_impl_hook;
		utils::hook::detour x_game_save_files_get_folder_with_ui_result_hook;
		utils::hook::detour x_store_query_game_license_result_hook;
		utils::hook::detour x_user_is_store_user_hook;



		std::string guid_to_string(GUID guid) {
			return std::format("{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", guid.Data1, guid.Data2, guid.Data3,
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
		}

		bool is_target_api(GUID guid_first, GUID guid_second, const std::string& guid_first_target, const std::string& guid_second_target) {
			return guid_to_string(guid_first) == guid_first_target && guid_to_string(guid_second) == guid_second_target;
		}

		void* get_vt_function(void* class_ptr, std::size_t index) {
			struct vt_cls { void** vftable_; };
			return PTR_AS(vt_cls*, class_ptr)->vftable_[index];
		}

		namespace x_game_save {
			HRESULT x_game_save_files_get_folder_with_ui_result_stub(void* _this, uwp::XAsyncBlock* async, std::size_t folder_size, char* folder_result) {
				if (_getcwd(folder_result, folder_size) == nullptr) {
					return x_game_save_files_get_folder_with_ui_result_hook.invoke<HRESULT>(_this, async, folder_size, folder_result);
				}

				return S_OK;
			}
		}

		
        
        
        std::unordered_set<uwp::XAsyncBlock*> fake_async_blocks;

        static DWORD WINAPI fake_async_thread(LPVOID param) {
            uwp::XAsyncBlock* async = (uwp::XAsyncBlock*)param;
            Sleep(1);
            void(*cb)(uwp::XAsyncBlock*) = *(void(**)(uwp::XAsyncBlock*))((uint8_t*)async + 16);
            if (cb) cb(async);
            return 0;
        }

        namespace x_package {
            std::unordered_set<void*> fake_mount_handles;

            static utils::hook::detour x_package_mount_with_ui_result_hook;
            static utils::hook::detour x_package_get_mount_path_size_hook;
            static utils::hook::detour x_package_get_mount_path_hook;
            static utils::hook::detour x_package_close_mount_handle_hook;
            static utils::hook::detour x_package_enumerate_packages_v8_hook;

            typedef bool(__stdcall* PkgEnumCb_t)(void* ctx, const XPackageDetails* details);

            struct PkgEnumCtx {
                void* orig_cb;
                void* orig_ctx;
            };

            static bool __stdcall hook_pkg_enum_callback(void* ctx, const XPackageDetails* details) {
                if (!ctx || !details) return true;
                PkgEnumCtx* c = reinterpret_cast<PkgEnumCtx*>(ctx);
                
                typedef bool(__stdcall* PkgEnumCb_t)(void*, const XPackageDetails*);
                PkgEnumCb_t orig = reinterpret_cast<PkgEnumCb_t>(c->orig_cb);

                if (details && details->kind == 1 && details->storeId && details->packageIdentifier) {
                    LOG("UWP", INFO, "Intercepted real package enum! ID: {}, StoreID: {}", details->packageIdentifier, details->storeId);
                    
                    // Copy details and force StoreId to Season Pass so the game thinks it owns the product
                    XPackageDetails fake_details = *details;
                    fake_details.storeId = "9NCV29923SWQ";
                    
                    return orig(c->orig_ctx, &fake_details);
                }

                return orig(c->orig_ctx, details);
            }

            HRESULT x_package_enumerate_packages_v8_stub(void* _this, uint32_t kind, uint32_t scope, void* ctx, void* cb) {
                auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, uint32_t, uint32_t, void*, void*)>(x_package_enumerate_packages_v8_hook.get_original());
                
                PkgEnumCtx wrapper_ctx = { cb, ctx };
                void* call_cb = cb ? (void*)&hook_pkg_enum_callback : nullptr;
                void* call_ctx = cb ? (void*)&wrapper_ctx : ctx;

                HRESULT hr = func(_this, kind, scope, call_ctx, call_cb);
                
                return hr;
            }

            static utils::hook::detour x_package_mount_with_ui_async_hook;

            HRESULT x_package_mount_with_ui_async_stub(void* _this, const char* package_identifier, uwp::XAsyncBlock* async) {
                LOG("UWP", INFO, "XPackageMountWithUiAsync called for package: {}", package_identifier);
                
                // For ANY package that is not the base game, we FAKE the mount!
                // This prevents the OS from returning 803F8001 and blocking the game from proceeding.
                // The user must place the DLC files in the base game directory.
                fake_async_blocks.insert(async);
                HANDLE h = CreateThread(nullptr, 0, fake_async_thread, async, 0, nullptr);
                if (h) CloseHandle(h);
                return S_OK;
            }

            HRESULT x_package_mount_with_ui_result_stub(void* _this, uwp::XAsyncBlock* async, void** out_handle) {
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

            HRESULT x_package_get_mount_path_size_stub(void* _this, void* handle, uint64_t* out_size) {
                if (fake_mount_handles.contains(handle)) {
                    if (out_size) {
                        char path[MAX_PATH];
                        GetModuleFileNameA(nullptr, path, MAX_PATH);
                        std::string dir(path);
                        auto pos = dir.find_last_of("\\/");
                        dir = (pos != std::string::npos) ? dir.substr(0, pos) : dir;
                        *out_size = dir.size() + 1;
                        LOG("UWP", INFO, "Faked XPackageGetMountPathSize to: {}", *out_size);
                    }
                    return S_OK;
                }
                auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, uint64_t*)>(x_package_get_mount_path_size_hook.get_original());
                return func(_this, handle, out_size);
            }

            HRESULT x_package_get_mount_path_stub(void* _this, void* handle, uint64_t out_size, char* out_buf) {
                if (fake_mount_handles.contains(handle)) {
                    if (out_buf) {
                        char path[MAX_PATH];
                        GetModuleFileNameA(nullptr, path, MAX_PATH);
                        std::string dir(path);
                        auto pos = dir.find_last_of("\\/");
                        dir = (pos != std::string::npos) ? dir.substr(0, pos) : dir;
                        strncpy_s(out_buf, out_size, dir.c_str(), _TRUNCATE);
                        LOG("UWP", INFO, "Faked XPackageGetMountPath to: {}", out_buf);
                    }
                    return S_OK;
                }
                auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, uint64_t, char*)>(x_package_get_mount_path_hook.get_original());
                return func(_this, handle, out_size, out_buf);
            }

            void x_package_close_mount_handle_stub(void* _this, void* handle) {
                if (fake_mount_handles.contains(handle)) {
                    fake_mount_handles.erase(handle);
                    free(handle);
                }
            }
        }


		namespace x_store {
            static utils::hook::detour x_store_enumerate_products_query_hook;
            static utils::hook::detour x_store_query_entitled_products_async_hook;
            static utils::hook::detour x_store_acquire_license_for_package_async_hook;
            static utils::hook::detour x_store_acquire_license_for_package_result_hook;
            static utils::hook::detour x_store_license_is_valid_hook;
            static utils::hook::detour x_store_close_license_handle_hook;

            static utils::hook::detour x_store_acquire_license_for_durables_async_hook;
            static utils::hook::detour x_store_acquire_license_for_durables_result_hook;

            HRESULT x_store_acquire_license_for_durables_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, const char* store_id, uwp::XAsyncBlock* async) {
                LOG("UWP", INFO, "XStoreAcquireLicenseForDurablesAsync called for StoreId: {}", store_id ? store_id : "null");
                auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, const char*, void*)>(x_store_acquire_license_for_durables_async_hook.get_original());
                HRESULT hr = func(_this, store_context_handle, store_id, async);
                LOG("UWP", INFO, "XStoreAcquireLicenseForDurablesAsync synchronous hr: {:X}", static_cast<uint32_t>(hr));
                if (FAILED(hr)) {
                    LOG("UWP", INFO, "Synchronous failure! Falling back to FAKE license acquisition for Durables.");
                    fake_async_blocks.insert(async);
                    HANDLE h = CreateThread(nullptr, 0, fake_async_thread, async, 0, nullptr);
                    if (h) CloseHandle(h);
                    return S_OK;
                }
                return hr;
            }

            HRESULT x_store_acquire_license_for_durables_result_stub(uwp::IStoreImpl1* _this, uwp::XAsyncBlock* async, void** out_license) {
                if (fake_async_blocks.contains(async)) {
                    fake_async_blocks.erase(async);
                    if (out_license) {
                        void* fake = malloc(0x30);
                        memset(fake, 0, 0x30);
                        ((uint8_t*)fake)[0x18] = 1; // OFF_LICENSE_VALID
                        *out_license = fake;
                        LOG("UWP", INFO, "XStoreAcquireLicenseForDurablesResult: FAKE async -> Injected fake license");
                    }
                    return S_OK;
                }
                auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, void**)>(x_store_acquire_license_for_durables_result_hook.get_original());
                HRESULT hr = func(_this, async, out_license);
                if (SUCCEEDED(hr) && out_license && *out_license) {
                    ((uint8_t*)*out_license)[0x18] = 1; // Force license valid
                    LOG("UWP", INFO, "XStoreAcquireLicenseForDurablesResult: Forced real license to valid");
                } else if (FAILED(hr) && out_license) {
                    void* fake = malloc(0x30);
                    memset(fake, 0, 0x30);
                    ((uint8_t*)fake)[0x18] = 1; // OFF_LICENSE_VALID
                    *out_license = fake;
                    LOG("UWP", INFO, "XStoreAcquireLicenseForDurablesResult: FAILED ({:X}) -> Injected fake license!", static_cast<uint32_t>(hr));
                    hr = S_OK;
                }
                return hr;
            }

            HRESULT x_store_acquire_license_for_package_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, const char* package_identifier, uwp::XAsyncBlock* async) {
                if (package_identifier && strncmp(package_identifier, "FakeDLC.", 8) == 0) {
                    LOG("UWP", INFO, "Faking XStoreAcquireLicenseForPackageAsync for package: {}", package_identifier);
                    fake_async_blocks.insert(async);
                    HANDLE h = CreateThread(nullptr, 0, fake_async_thread, async, 0, nullptr);
                    if (h) CloseHandle(h);
                    return S_OK;
                }
                
                LOG("UWP", INFO, "XStoreAcquireLicenseForPackageAsync called for REAL package: {}", package_identifier);
                auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, const char*, void*)>(x_store_acquire_license_for_package_async_hook.get_original());
                HRESULT hr = func(_this, store_context_handle, package_identifier, async);
                LOG("UWP", INFO, "XStoreAcquireLicenseForPackageAsync synchronous hr: {:X}", static_cast<uint32_t>(hr));
                
                if (FAILED(hr)) {
                    LOG("UWP", INFO, "Synchronous failure! Falling back to FAKE license acquisition for REAL package.");
                    fake_async_blocks.insert(async);
                    HANDLE h = CreateThread(nullptr, 0, fake_async_thread, async, 0, nullptr);
                    if (h) CloseHandle(h);
                    return S_OK;
                }
                
                return hr;
            }

            struct ProductsCbCtx {
                bool(*userCb)(const uwp::XStoreProduct*, void*) = nullptr;
                void* userCtx = nullptr;
            };

            static void PatchProductSkuData(uint8_t* product) {
                uint32_t skuCount = *(uint32_t*)(product + 160); // OFF_SKU_COUNT
                uint8_t* skuArr   = *(uint8_t**)(product + 168); // OFF_SKU_PTR
                if (!skuArr || skuCount == 0) return;

                for (uint32_t i = 0; i < skuCount && i < 64; i++) {
                    uint8_t* sku = skuArr + (size_t)i * 0x110; // SKU_STRIDE
                    sku[121]  = 1; // SKU_OFF_HAS_COLL
                    sku[120]  = 0; // SKU_OFF_IS_TRIAL
                    *(int64_t*)(sku + 128)  = 133484064000000000LL; // SKU_OFF_COLL_ACQUIRED
                    *(int64_t*)(sku + 136)  = 133484064000000000LL; // SKU_OFF_COLL_START
                    *(int64_t*)(sku + 144)  = 0x7FFFFFFFFFFFFFFELL; // SKU_OFF_COLL_END
                    sku[152]                 = 0; // SKU_OFF_COLL_IS_TRIAL
                    *(uint32_t*)(sku + 156) = 0; // SKU_OFF_COLL_TRIAL_SEC
                    *(uint32_t*)(sku + 160)  = 1; // SKU_OFF_COLL_QUANTITY
                }
            }

            static bool hook_get_products_callback(const uwp::XStoreProduct* product, void* ctx) {
                // Force ownership on product
                auto* p = const_cast<uwp::XStoreProduct*>(product);
                p->has_digital_download_ = true;
                p->is_in_user_collection_ = true;

                // Force ownership on all SKUs using byte offsets to avoid compiler ICE
                uint8_t* prodBytes = reinterpret_cast<uint8_t*>(p);
                uint32_t skuCount = *(uint32_t*)(prodBytes + 0xA0); // OFF_SKU_COUNT
                uint8_t* skuArr = *(uint8_t**)(prodBytes + 0xA8);   // OFF_SKU_PTR
                if (skuArr && skuCount > 0) {
                    for (uint32_t i = 0; i < skuCount && i < 64; i++) {
                        uint8_t* sku = skuArr + (size_t)i * 0x110; // SKU_STRIDE
                        sku[121]  = 1; // SKU_OFF_HAS_COLL
                        sku[120]  = 0; // SKU_OFF_IS_TRIAL
                        *(int64_t*)(sku + 128)  = 133484064000000000LL; // FAKE_ACQUIRED
                        *(int64_t*)(sku + 136)  = 133484064000000000LL; // FAKE_ACQUIRED
                        *(int64_t*)(sku + 144)  = 0x7FFFFFFFFFFFFFFELL; // FAKE_END
                        sku[152]                 = 0; // SKU_OFF_COLL_IS_TRIAL
                        *(uint32_t*)(sku + 156)  = 0; // SKU_OFF_COLL_TRIAL_SEC
                        *(uint32_t*)(sku + 160)  = 1; // SKU_OFF_COLL_QUANTITY
                    }
                }
                
                LOG("UWP", INFO, "XStoreProduct patched! id={} title={} owned={}", p->store_id_, p->title_, p->is_in_user_collection_);

                auto* c = reinterpret_cast<ProductsCbCtx*>(ctx);
                return c->userCb ? c->userCb(product, c->userCtx) : true;
            }

            HRESULT x_store_enumerate_products_query_stub(uwp::IStoreImpl1* _this, const uwp::XStoreProductQueryHandle product_query_handle, void* context, bool(*callback)(const uwp::XStoreProduct*, void*)) {
                ProductsCbCtx cbCtx;
                cbCtx.userCb = callback;
                cbCtx.userCtx = context;

                void* callCtx = callback ? (void*)&cbCtx : context;
                void* callCb  = callback ? (void*)&hook_get_products_callback : nullptr;

                HRESULT hr = x_store_enumerate_products_query_hook.invoke<HRESULT>(_this, product_query_handle, callCtx, callCb);

                if (!callback && hr == S_OK && product_query_handle) {
                    auto* base = *(uint8_t**)((uint8_t*)product_query_handle + 24); // OFF_ARR_START
                    auto* end  = *(uint8_t**)((uint8_t*)product_query_handle + 32); // OFF_ARR_END
                    for (uint8_t* p = base; base && p < end; p += 208) { // STRIDE
                        if (!p[145]) { p[145] = 1; }
                        if (!p[144]) { p[144] = 1; }
                        PatchProductSkuData(p);
                    }
                }
                return hr;
            }

            HRESULT x_store_query_entitled_products_async_stub(uwp::IStoreImpl1* _this, const uwp::XStoreContextHandle store_context_handle, uwp::XStoreProductKind product_kinds, std::uint32_t max_items_to_retrieve_per_page, uwp::XAsyncBlock* async) {
                LOG("UWP", INFO, "Intercepted XStoreQueryEntitledProductsAsync! Redirecting to XStoreQueryAssociatedProductsAsync to fetch unowned DLCs...");
                // Call XStoreQueryAssociatedProductsAsync instead (vtable index 5)
                auto func = get_vt_function(_this, 5);
                typedef HRESULT(__stdcall* QueryAssociatedT)(uwp::IStoreImpl1*, uwp::XStoreContextHandle, uwp::XStoreProductKind, std::uint32_t, uwp::XAsyncBlock*);
                return ((QueryAssociatedT)func)(_this, store_context_handle, product_kinds, max_items_to_retrieve_per_page, async);
            }

            HRESULT x_store_acquire_license_for_package_result_stub(uwp::IStoreImpl1* _this, uwp::XAsyncBlock* async, void** out_license) {
                if (fake_async_blocks.contains(async)) {
                    fake_async_blocks.erase(async);
                    if (out_license) {
                        void* fake = malloc(0x30);
                        memset(fake, 0, 0x30);
                        ((uint8_t*)fake)[0x18] = 1; // OFF_LICENSE_VALID
                        *out_license = fake;
                        LOG("UWP", INFO, "Faked XStoreAcquireLicenseForPackageResult for FAKE package!");
                    }
                    return S_OK;
                }
                auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*, void**)>(x_store_acquire_license_for_package_result_hook.get_original());
                HRESULT hr = func(_this, async, out_license);
                if (SUCCEEDED(hr) && out_license && *out_license) {
                    ((uint8_t*)*out_license)[0x18] = 1; // Force license valid
                    LOG("UWP", INFO, "XStoreAcquireLicenseForPackageResult: Forced real license to valid");
                } else if (FAILED(hr) && out_license) {
                    void* fake = malloc(0x30);
                    memset(fake, 0, 0x30);
                    ((uint8_t*)fake)[0x18] = 1; // OFF_LICENSE_VALID
                    *out_license = fake;
                    LOG("UWP", INFO, "XStoreAcquireLicenseForPackageResult: FAILED ({:X}) -> Injected fake license!", static_cast<uint32_t>(hr));
                    hr = S_OK;
                }
                return hr;
            }

            HRESULT x_store_license_is_valid_stub(void* _this, void* license) {
                if (license) {
                    uint8_t* ptr = reinterpret_cast<uint8_t*>(license);
                    if (ptr[0x18] == 1) { // is_valid from our fake
                        LOG("UWP", INFO, "LicenseIsValid: returning true for FAKE license");
                        return 1; // S_OK / true
                    }
                }
                auto func = reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(x_store_license_is_valid_hook.get_original());
                return func(_this, license);
            }

            void x_store_close_license_handle_stub(uwp::IStoreImpl1* _this, void* handle) {
                if (handle) {
                    free(handle);
                }
            }

			HRESULT x_store_query_game_license_result_stub(uwp::IStoreImpl1* _this, uwp::XAsyncBlock* async, uwp::XStoreGameLicense* license) {
				// result ignored, we return S_OK anyway to avoid "popup_drm_menu_gdk_license_error"
				x_store_query_game_license_result_hook.invoke<HRESULT>(_this, async, license);

				// bypass "popup_drm_menu_gdk_invalid_license"
				license->is_active_ = true;
				license->is_disc_license_ = false;
				license->is_trial_ = false;

				return S_OK;
			}
		}

		namespace x_user {
			bool x_user_is_store_user_stub(void* _this, uwp::XUserHandle user) {
				return true;
			}
		}

		HRESULT query_api_impl_stub(GUID* first, GUID* second, void** api_out) {
			auto res = query_api_impl_hook.invoke<HRESULT>(first, second, api_out);
			static bool hooked_game_save_vt = false;
			static bool hooked_store_vt = false;
			static bool hooked_user_vt = false;

			if (first != nullptr && second != nullptr && res >= 0) {
                static bool hooked_pkg_vt = false;
				if (!hooked_pkg_vt && is_target_api(*first, *second, "af406016-e850-4aa8-a88d-2f3dcb9dac7e", "e2a4734b-2f4a-456d-aa8f-d065e04fb209")) {
                    x_package::x_package_enumerate_packages_v8_hook.create(get_vt_function(*api_out, 39), x_package::x_package_enumerate_packages_v8_stub);
                    // MountWithUiAsync is vtable index 37
                    x_package::x_package_mount_with_ui_async_hook.create(get_vt_function(*api_out, 37), x_package::x_package_mount_with_ui_async_stub);
                    // MountWithUiResult is vtable index 38
                    x_package::x_package_mount_with_ui_result_hook.create(get_vt_function(*api_out, 38), x_package::x_package_mount_with_ui_result_stub);
                    // GetMountPathSize is vtable index 24
                    x_package::x_package_get_mount_path_size_hook.create(get_vt_function(*api_out, 24), x_package::x_package_get_mount_path_size_stub);
                    // GetMountPath is vtable index 25
                    x_package::x_package_get_mount_path_hook.create(get_vt_function(*api_out, 25), x_package::x_package_get_mount_path_stub);
                    // CloseMountHandle is vtable index 26
                    x_package::x_package_close_mount_handle_hook.create(get_vt_function(*api_out, 26), x_package::x_package_close_mount_handle_stub);
                    
                    hooked_pkg_vt = true;
                    LOG("UWP", INFO, "Hooked XPackage interface!");
                }

				if (!hooked_game_save_vt && is_target_api(*first, *second, "704c3f58-e629-4cc2-b197-30511b996fe2", "704c3f58-e629-4cc2-b197-30511b996ee2")) {
					if (identification::game::get_target_game() == "Black Ops III"s) {
						x_game_save_files_get_folder_with_ui_result_hook.create(get_vt_function(*api_out, 31),
							x_game_save::x_game_save_files_get_folder_with_ui_result_stub);
					}
					hooked_game_save_vt = true;
				}

				if (!hooked_store_vt && is_target_api(*first, *second, "0dd112ac-7c24-448c-b92b-3960fb5bd30c", "5c48dedf-0b67-4492-a4b5-6829b8e796e1")) {
					
                    x_store::x_store_query_entitled_products_async_hook.create(get_vt_function(*api_out, 9), x_store::x_store_query_entitled_products_async_stub);
                    x_store::x_store_enumerate_products_query_hook.create(get_vt_function(*api_out, 15), x_store::x_store_enumerate_products_query_stub);
					x_store::x_store_acquire_license_for_package_async_hook.create(get_vt_function(*api_out, 20), x_store::x_store_acquire_license_for_package_async_stub);
					x_store::x_store_acquire_license_for_package_result_hook.create(get_vt_function(*api_out, 21), x_store::x_store_acquire_license_for_package_result_stub);
					x_store::x_store_license_is_valid_hook.create(get_vt_function(*api_out, 22), x_store::x_store_license_is_valid_stub);
					x_store::x_store_close_license_handle_hook.create(get_vt_function(*api_out, 23), x_store::x_store_close_license_handle_stub);

					x_store_query_game_license_result_hook.create(get_vt_function(*api_out, 29), x_store::x_store_query_game_license_result_stub);

                    // Map Durables license acquisition to the same stubs since we want to fake them if they fail
                    x_store::x_store_acquire_license_for_durables_async_hook.create(get_vt_function(*api_out, 30), x_store::x_store_acquire_license_for_durables_async_stub);
                    x_store::x_store_acquire_license_for_durables_result_hook.create(get_vt_function(*api_out, 31), x_store::x_store_acquire_license_for_durables_result_stub);

					hooked_store_vt = true;
                    LOG("UWP", INFO, "Hooked XStore interface!");
				}

				if (!hooked_user_vt && is_target_api(*first, *second, "01acd177-91f9-4763-a38e-ccbb55ce32e0", "079415e3-6727-437f-8e9d-8f8f9b2439f7")) {
					x_user_is_store_user_hook.create(get_vt_function(*api_out, 42), x_user::x_user_is_store_user_stub);
					hooked_user_vt = true;
				}
			}
			return res;
		}

		void hook_query_api() {
			static bool hooked = false;
			if (hooked) return;

			utils::nt::library lib("xgameruntime.dll");
			if (!lib.is_valid()) return;

			auto proc = lib.get_proc<FARPROC>("QueryApiImpl");
			if (proc) {
				query_api_impl_hook.create(proc, query_api_impl_stub);
				hooked = true;
				LOG("UWP", INFO, "Successfully hooked QueryApiImpl without touching GetProcAddress!");
			}
		}
	}

	struct component final : generic_component {
		        void post_load() override {
            utils::nt::library game{};
            if (game.get_iat_entry("XCurl.dll", "curl_multi_poll") == nullptr) {
                return;
            }

            int msgboxID = MessageBoxA(
                NULL,
                "Do you want to load the INTERNAL XStore loader?\n\nYes = Internal ZeroProxy hooks (DLC Unlocker)\nNo = External XGameRuntime.dll",
                "ZeroProxy - XStore Loader",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1 | MB_TOPMOST
            );

            int internal_load = (msgboxID == IDYES) ? 1 : 0;
            int external_load = (msgboxID == IDNO) ? 1 : 0;

            if (external_load) {
                const char* buffer = "XGameRuntime.dll";
                HMODULE hMod = LoadLibraryA(buffer);
                if (hMod) {
                    LOG("UWP", INFO, "Loaded external XStore DLL: {}", buffer);
                } else {
                    LOG("UWP", ERROR, "Failed to load external XStore DLL: {}", buffer);
                }
            }

            if (internal_load) {
                LOG("UWP", INFO, "Internal XStore loader activated.");
                scheduler::loop(hook_query_api, scheduler::pipeline::async);
            }
        }
	};
}
#endif
