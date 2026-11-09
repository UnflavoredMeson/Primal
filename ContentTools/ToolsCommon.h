// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <wrl.h>

#ifndef EDITOR_INTERFACE
#if defined(_MSC_VER)
#define EDITOR_INTERFACE extern "C" __declspec(dllexport)
#else
#define EDITOR_INTERFACE extern "C" __attribute__((visibility("default")))
#endif
#endif

class Progression
{
  public:
    using progress_callback = void (*)(s32, s32);

    Progression() = default;
    explicit Progression(progress_callback callback)
        : _callback{callback}
    {
    }

    DISABLE_COPY(Progression);

    auto callback(u32 value, u32 max_value) -> void
    {
        _value = value;
        _max_value = max_value;
        if (_callback)
        {
            _callback(value, max_value);
        }
    }

    [[nodiscard]] constexpr auto max_value() const -> u32
    {
        return _max_value;
    }
    [[nodiscard]] constexpr auto value() const -> u32
    {
        return _value;
    }

  private:
    progress_callback _callback{nullptr};
    u32 _value{0};
    u32 _max_value{0};
};

inline auto file_exists(const char *file) -> bool
{
    const DWORD attr{::GetFileAttributesA(file)};
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

inline auto to_wstring(const char *cstr) -> std::wstring
{
    std::string s{cstr};
    return {s.begin(), s.end()};
}

inline auto split(const std::string &s, char delimiter) -> primal::utl::vector<std::string>
{
    size_t start{0};
    size_t end{0};
    std::string substring;
    primal::utl::vector<std::string> strings;

    while ((end = s.find(delimiter, start)) != std::string::npos)
    {
        substring = s.substr(start, end - start);
        start = end + sizeof(char);
        strings.emplace_back(substring);
    }

    strings.emplace_back(s.substr(start));
    return strings;
}
