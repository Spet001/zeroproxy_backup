#include "common.hpp"
#include "component/lua_hook.hpp"
#include "game/game.hpp"

#include <loader/component_loader.hpp>
#include <memory/memory.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <cstdint>

namespace mods {
	namespace {
		constexpr std::uint32_t fnv_base_32 = 0x4B9ACE2F;

		inline std::uint32_t fnv1a(const char* key) {
			const char* data = key;
			std::uint32_t hash = 0x4B9ACE2F;
			while (*data) {
				hash ^= *data;
				hash *= 0x1000193;
				data++;
			}
			hash *= 0x1000193; // bo3 hashing multiplier
			return hash;
		}

		struct workshop_json {
			enum workshop_json_parserstate {
				WJSPS_OPEN_BRACE,
				WJSPS_READY,
				WJSPS_TOKEN,
				WJSPS_DONE
			};
			
		private:
			char* __internal_data;
			std::unordered_map<std::uint32_t, const char*> props;
			std::size_t data_len;
			
			void clear() {
				if (__internal_data) {
					delete[] __internal_data;
					__internal_data = nullptr;
				}
				props.clear();
			}

			char* make_buf(std::size_t len) {
				clear();
				__internal_data = new char[len];
				std::memset(__internal_data, 0, len);
				data_len = len;
				return __internal_data;
			}

		public:
			bool parse(std::ifstream& inFile) {
				inFile.seekg(0, std::ios::end);
				std::size_t fileSize = inFile.tellg();
				inFile.seekg(0, std::ios::beg);
				inFile.read(make_buf(fileSize + 1), fileSize);

				char* current_index = __internal_data;
				char* current_key = nullptr;
				char* current_value = nullptr;

				workshop_json_parserstate state = WJSPS_OPEN_BRACE;
				bool isKey = true;
				bool escaped = false;

				while (*current_index && (current_index < (__internal_data + data_len)) && (state != WJSPS_DONE)) {
					switch (state) {
						case WJSPS_OPEN_BRACE:
							if (*current_index++ != '{') break;
							state = WJSPS_READY;
							break;
						case WJSPS_READY: {
							auto ch = *current_index++;
							if (ch == '"') {
								state = WJSPS_TOKEN;
								if (isKey) current_key = current_index;
								else current_value = current_index;
							} else if (ch == '}') {
								state = WJSPS_DONE;
							}
							break;
						}
						case WJSPS_TOKEN: {
							auto ch = *current_index;
							if (ch == '"') {
								if (!escaped) {
									*current_index = 0;
									if (!isKey) {
										props[fnv1a(current_key)] = current_value;
										current_key = current_value = nullptr;
									}
									isKey = !isKey;
									escaped = false;
									state = WJSPS_READY;
								}
							}
							escaped = !escaped && (ch == '\\');
							current_index++;
							break;
						}
					}
				}
				return state == WJSPS_DONE;
			}

			const char* find(const char* key) {
				auto it = props.find(fnv1a(key));
				return (it == props.end()) ? nullptr : it->second;
			}

			bool is_mod() {
				auto val = find("Type");
				return !val || std::strcmp(val, "map");
			}

			const char* title() {
				auto val = find("Title");
				return val ? val : "<Error: Unknown Mod>";
			}

			const char* folder_name() {
				auto val = find("FolderName");
				return val ? val : "";
			}

			const char* description() {
				auto val = find("Description");
				return val ? val : "";
			}

			workshop_json() {
				__internal_data = nullptr;
				data_len = 0;
			}

			~workshop_json() {
				clear();
			}
		};

		utils::hook::detour populate_ugc_list_hook;
		utils::hook::detour lua_cod_lua_call_mods_is_subscribed_item_hook;
		utils::hook::detour lua_cod_lua_call_mods_subscribe_ugc_hook;
		utils::hook::detour lua_cod_lua_call_mods_install_progress_ugc_hook;
		utils::hook::detour lua_cod_lua_call_mods_installed_ugc_hook;

		void update_local_ugc(bool isMod) {
			auto dest_list = isMod ? PTR_AS(t7s::ugcinfo_wstor*, 0x18A58F60) : PTR_AS(t7s::ugcinfo_wstor*, 0x18A7ED68);
			
			std::string folder = isMod ? "mods" : "usermaps";
			if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
				return;
			}

			for (const auto& entry : std::filesystem::directory_iterator(folder)) {
				if (!entry.is_directory()) continue;

				auto ws_json_path = entry.path() / "workshop.json";
				if (!std::filesystem::exists(ws_json_path)) continue;

				std::ifstream inFile(ws_json_path, std::ios::binary);
				if (!inFile.is_open()) continue;

				workshop_json json;
				if (!json.parse(inFile)) {
					inFile.close();
					continue;
				}
				inFile.close();

				if (json.is_mod() != isMod) continue;

				if (dest_list->num_entries_ >= t7s::WORKSHOP_MAX_ENTRIES) {
					break;
				}

				t7s::ugcinfo_entry_wstor* dest_entry = dest_list->entries_ + dest_list->num_entries_++;
				std::memset(dest_entry, 0, sizeof(t7s::ugcinfo_entry_wstor));

				auto pathOnDiskStr = entry.path().string();
				const char* pathOnDisk = pathOnDiskStr.c_str();
				
				// Generate a fake file ID using FNV hash of the folder name
				std::uint64_t fakeId = fnv1a(json.folder_name());

				strncpy_s(dest_entry->name_, json.title(), _TRUNCATE);
				strncpy_s(dest_entry->internal_name_, json.folder_name(), _TRUNCATE);
				snprintf(dest_entry->ugc_name_, sizeof(dest_entry->ugc_name_), "%llu", fakeId);
				strncpy_s(dest_entry->description_, json.description(), _TRUNCATE);
				strncpy_s(dest_entry->ugc_path_, pathOnDisk, _TRUNCATE);

				auto sPos = strstr(pathOnDisk, "311210");
				if (sPos) {
					strncpy_s(dest_entry->ugc_path_basic_, sPos, _TRUNCATE);
					strncpy_s(dest_entry->ugc_root_, pathOnDisk, sPos - pathOnDisk - 1);
				}

				auto fs_game = dest_entry->ugc_name_;
				auto v39 = *fs_game;
				for (dest_entry->hash_id_ = 5381; *fs_game; v39 = *fs_game) {
					++fs_game;
					dest_entry->hash_id_ = tolower(v39) + 33 * dest_entry->hash_id_;
				}

				dest_entry->unk_4B0_ = 1;
				dest_entry->unk_4B8_ = isMod ? 1 : 2;
			}
		}

		void populate_ugc_list_stub(std::uint64_t a1, unsigned char a2) {
			std::uintptr_t return_address = PTR_AS(std::uintptr_t, _ReturnAddress());
			// The original hook checked 0x21EF73A, which is the call from Mods_UpdateModsList
			update_local_ugc(return_address != 0x21EF73A);
			populate_ugc_list_hook.invoke<void>(a1, a2);
		}

		int lua_cod_lua_call_mods_is_subscribed_item_stub(t7s::lua_State* luaVM) {
			game::lua_pushboolean(luaVM, 1);
			return 1;
		}

		int lua_cod_lua_call_mods_subscribe_ugc_stub(t7s::lua_State* luaVM) {
			game::lua_pushboolean(luaVM, 1);
			return 1;
		}

		int lua_cod_lua_call_mods_install_progress_ugc_stub(t7s::lua_State* luaVM) {
			game::lua_pushnumber(luaVM, 1.0f);
			return 1;
		}

		int lua_cod_lua_call_mods_installed_ugc_stub(t7s::lua_State* luaVM) {
			game::lua_pushboolean(luaVM, 1);
			return 1;
		}
	}

	struct component final : generic_component {
		void post_load() override {
			// Hook populate_ugc_list
			static utils::nt::library game{};
			
			// Using the direct MS Store address to match ResurgeGDK
			populate_ugc_list_hook.create(0x21EF320, populate_ugc_list_stub);

			// Hook Lua Functions to bypass Steam requirements
			lua_hook::create(lua_cod_lua_call_mods_is_subscribed_item_hook, "Mods_IsSubscribedItem", lua_cod_lua_call_mods_is_subscribed_item_stub);
			lua_hook::create(lua_cod_lua_call_mods_subscribe_ugc_hook, "Mods_SubscribeUGC", lua_cod_lua_call_mods_subscribe_ugc_stub);
			lua_hook::create(lua_cod_lua_call_mods_install_progress_ugc_hook, "Mods_InstallProgressUGC", lua_cod_lua_call_mods_install_progress_ugc_stub);
			lua_hook::create(lua_cod_lua_call_mods_installed_ugc_hook, "Mods_InstalledUGC", lua_cod_lua_call_mods_installed_ugc_stub);
		}
	};
}

REGISTER_COMPONENT(mods::component)
