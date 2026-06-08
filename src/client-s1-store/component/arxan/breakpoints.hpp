#pragma once
#include <utils/hook.hpp>

namespace arxan::breakpoints
{
    extern utils::hook::detour add_vectored_exception_handler_hook;
    extern utils::hook::detour remove_vectored_exception_handler_hook;
}
