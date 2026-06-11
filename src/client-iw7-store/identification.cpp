#include <Windows.h>

extern "C" const char* ext_get_client_name() {
	return "iw7s";
}

extern "C" const char* ext_get_target_game() {
	return "Infinite Warfare";
}

extern "C" void migrate_if_needed() {
	// TODO: move and implement elsewhere
}
