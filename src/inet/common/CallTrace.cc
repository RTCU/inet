//
// Copyright (C) 2024 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#include "inet/common/CallTrace.h"

#ifdef __GNUC__

#include <cxxabi.h>
#include <dlfcn.h>

namespace inet {

CallTrace::State CallTrace::state;

CallTrace::CallTrace(const char *name, int maxLevel, const char *filter)
{
    state.enabled = true;
    state.level = 1;
    state.name = name;
    state.maxLevel = maxLevel;
    state.filter = std::regex(filter);
    printf("TRACE -> %s\n", state.name);
}

CallTrace::~CallTrace()
{
    printf("TRACE <- %s\n", state.name);
    state.enabled = false;
    state.level = -1;
    state.name = nullptr;
    state.maxLevel = -1;
    state.filter = std::regex();
}

std::string CallTrace::demangle(const char* mangledName) {
    int status = -1;
    // The abi::__cxa_demangle function allocates memory for the demangled name using malloc,
    // so we use a unique_ptr with a custom deleter to ensure that it gets freed.
    std::unique_ptr<char, void(*)(void*)> res{
        abi::__cxa_demangle(mangledName, NULL, NULL, &status),
        std::free
    };
    return (status == 0) ? res.get() : "Error demangling name";
}

} // namespace inet

extern "C"
{
    void __cyg_profile_func_enter(void* func, void* caller) __attribute__((no_instrument_function));
    void __cyg_profile_func_exit(void* func, void* caller) __attribute__((no_instrument_function));
}

void __cyg_profile_func_enter(void* func, void* caller)
{
    auto& state = inet::CallTrace::state;
    if (state.enabled) {
        state.enabled = false;
        if (1 <= state.level && state.level <= state.maxLevel) {
            Dl_info info;
            if (dladdr(func, &info)) {
                std::string functionName = inet::CallTrace::demangle(info.dli_sname);
                if (functionName != "CallTrace::~CallTrace()" &&
                    std::regex_match(functionName, state.filter))
                {
                    printf("TRACE %s-> %s\n",
                           std::string(state.level * 2, ' ').c_str(),
                           info.dli_sname ? functionName.c_str() : "?");
                    state.level++;
                }
            }
        }
        state.enabled = true;
    }
}

void __cyg_profile_func_exit(void* func, void* caller)
{
    auto& state = inet::CallTrace::state;
    if (state.enabled) {
        state.enabled = false;
        if (1 <= state.level && state.level <= state.maxLevel) {
            Dl_info info;
            if (dladdr(func, &info)) {
                std::string functionName = inet::CallTrace::demangle(info.dli_sname);
                if (functionName != "CallTrace::~CallTrace()" &&
                    std::regex_match(functionName, state.filter))
                {
                    state.level--;
                    printf("TRACE %s<- %s\n",
                           std::string(state.level * 2, ' ').c_str(),
                           info.dli_sname ? functionName.c_str() : "?");
                }
            }
        }
        state.enabled = true;
    }
}

#endif

