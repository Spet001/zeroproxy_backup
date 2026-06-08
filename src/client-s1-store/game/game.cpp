#include "common.hpp"
#include "game/game.hpp"

#include <memory/signature_store.hpp>
#include <utils/flags.hpp>

#define SETUP_POINTER(name) #name, (void**)&##name
#define SETUP_MOD(mod) [](memory::scanned_result<void> r) { return r.##mod##; }

void game::init() {
	memory::signature_store batch;

	batch.add(SETUP_POINTER(SEH_StringEd_GetString), "48 83 EC ? 48 8B 05 ? ? ? ? 48 85 C0 74 ? 80 78 ? ? 74 ? 48 0F BE 01");

	batch.scan_all();
}

bool game::is_server() {
	static bool is_server = utils::flags::has_flag("dedicated");
	return is_server;
}

std::uintptr_t game::get_base() {
	static const auto base = reinterpret_cast<std::uintptr_t>(utils::nt::library().get_ptr());
	return base;
}

bool game::environment::is_sp() {
	if (identification::game::get_version().mode_ == identification::game::mode::SP) return true;

	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string path_str = path;
	std::transform(path_str.begin(), path_str.end(), path_str.begin(), ::tolower);
	return path_str.find("s2_sp64_ship.exe") != std::string::npos;
}

bool game::environment::is_mp() {
	if (identification::game::get_version().mode_ == identification::game::mode::MP) return true;

	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string path_str = path;
	std::transform(path_str.begin(), path_str.end(), path_str.begin(), ::tolower);
	return path_str.find("s2_mp64_ship.exe") != std::string::npos;
}
