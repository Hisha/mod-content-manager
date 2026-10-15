#pragma once

#include <cstdint>
#include <string>

class ConfigMgr
{
public:
    static ConfigMgr* instance()
    {
        static ConfigMgr manager;
        return &manager;
    }

    template<class T>
    T GetOption(std::string const&, T const& fallback, bool = true) const
    {
        return fallback;
    }
};

#define sConfigMgr ConfigMgr::instance()
