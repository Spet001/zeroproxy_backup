#include "common.hpp"

extern "C" const char* ext_get_client_name() {
	return "s1s";
}

extern "C" const char* ext_get_target_game() {
	return "AW";
}

extern "C" void migrate_if_needed() {
	// TODO: move and implement elsewhere
}