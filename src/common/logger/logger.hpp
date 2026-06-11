#pragma once
#include "common_core.hpp"
#include "logger/console.hpp"
#include "logger/log_levels.hpp"

namespace logger {
	template <typename... Args>
	void print(const std::string& log_name, log_level level, const std::string_view& format, Args const &...args) {
		// TODO: Add caller check from backward-cpp
		std::string caller = "<unknown module>";

		auto message = std::vformat(format, std::make_format_args(args...));

		tm local_time;
		const time_t time_since_epoch = std::time(nullptr);
		localtime_s(&local_time, &time_since_epoch);


		auto console_timestamp = std::format("[{:0>2}:{:0>2}:{:0>2}]", local_time.tm_hour, local_time.tm_min, local_time.tm_sec);
		
		auto file_timestamp = std::format("[{:0>4}-{:0>2}-{:0>2} {:0>2}:{:0>2}:{:0>2}]", local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
			local_time.tm_hour, local_time.tm_min, local_time.tm_sec);
		
		char dll_path[MAX_PATH];
        HMODULE hModD3d11 = client_module;
        if (hModD3d11) {
            GetModuleFileNameA(hModD3d11, dll_path, MAX_PATH);
            std::string path(dll_path);
            auto pos = path.find_last_of("\\/");
            if (pos != std::string::npos) {
                path = path.substr(0, pos) + "\\ZeroProxy.log";
                FILE* log_file = nullptr;
                if (fopen_s(&log_file, path.c_str(), "a") == 0 && log_file) {
                    std::string plain_msg = std::format("{} [{}] ({}) {}\n", file_timestamp, level.get_label(), log_name, message);
                    fputs(plain_msg.c_str(), log_file);
                    fclose(log_file);
                } else {
                    char temp_path[MAX_PATH];
                    if (GetTempPathA(MAX_PATH, temp_path)) {
                        std::string fallback_path = std::string(temp_path) + "ZeroProxy.log";
                        if (fopen_s(&log_file, fallback_path.c_str(), "a") == 0 && log_file) {
                            std::string plain_msg = std::format("{} [{}] ({}) {}\n", file_timestamp, level.get_label(), log_name, message);
                            fputs(plain_msg.c_str(), log_file);
                            fclose(log_file);
                        }
                    }
                }
            }
        }

		console::write(std::format(ANSI_FG_CYAN "{} " ANSI_RESET "{}[{}{}] " ANSI_RESET ANSI_FG_RGB(0, 163, 163) "({}) " ANSI_RESET "{}" ANSI_RESET,

			console_timestamp, level.get_ansi_color(), caller.compare("<unknown module>") ? (caller + "/").c_str() : "", level.get_label(), log_name, message));
	}
}

#define LOG(name, type, ...) logger::print(name, logger::levels::type, __VA_ARGS__)
