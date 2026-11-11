// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

// Logging for the engine, the tools and game scripts (Docs/Multithreading.md, section 6).
//
//     log::info(log::Category::content, "Loaded {} entities", count);
//     log::error(log::Category::vulkan, "vkCreateDevice failed: {}", result);
//
// The format string is checked at compile time (std::format). Every function can be called from any thread, at
// any time: before initialize() and after shutdown() a message is written by the calling thread, in between by
// the logger thread. error() returns only when its message has reached the sinks.

#include "../Common/PrimitiveTypes.h"

#include <concepts>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace primal::log
{

enum class Level : u8
{
    debug,
    info,
    warning,
    error,
};

enum class Category : u8
{
    core,
    graphics,
    vulkan,
    d3d12,
    content,
    input,
    script,
    tools,

    count
};

// Messages below this level are compiled out. PRIMAL_LOG_LEVEL: 0 debug, 1 info, 2 warning, 3 error.
#if defined(PRIMAL_LOG_LEVEL)
inline constexpr Level compiled_level{static_cast<Level>(PRIMAL_LOG_LEVEL)};
#elif defined(_DEBUG)
inline constexpr Level compiled_level{Level::debug};
#else
inline constexpr Level compiled_level{Level::info};
#endif

// A message longer than this is cut, and ends in " [...]".
inline constexpr u32 max_message_length{2048};

// What a sink receives. 'text' and 'file' are only valid during the call.
struct Message
{
    u64 sequence{0};       // 1, 2, 3, ... in the order the sinks see them
    u64 time_ns{0};        // since the first message of the process
    std::string_view text; // UTF-8, without a line end
    const char *file{nullptr};
    u32 line{0};
    u32 thread{0}; // a small number per thread, in order of first use
    Level level{Level::info};
    Category category{Category::core};
};

// A copy of a message, as kept by the ring buffer.
struct StoredMessage
{
    u64 sequence{0};
    u64 time_ns{0};
    std::string text;
    const char *file{nullptr};
    u32 line{0};
    u32 thread{0};
    Level level{Level::info};
    Category category{Category::core};
};

// Called for every message, by one thread at a time. A sink must not log.
using sink_function = void (*)(const Message &message, void *user_data);

// Starts the logger thread. Without it everything still works, but each message is written by its caller.
auto initialize() -> void;
// Writes what is queued and stops the logger thread. Call it before the process ends.
auto shutdown() -> void;
// Returns when every message logged before the call has reached the sinks.
auto flush() -> void;

// Messages below 'level' are ignored at run time (default: compiled_level).
auto set_level(Level level) -> void;
[[nodiscard]] auto level() -> Level;

[[nodiscard]] auto add_sink(sink_function function, void *user_data) -> u32;
auto remove_sink(u32 sink_id) -> void;
// The built-in sink that writes to stderr (and to the debugger on Windows). On by default.
auto enable_console(bool enabled) -> void;

// The ring buffer keeps the most recent messages. Appends those with a sequence greater than 'after_sequence' to
// 'messages' (oldest first) and returns the sequence of the newest message there is: pass it to the next call.
auto read_recent(u64 after_sequence, std::vector<StoredMessage> &messages) -> u64;

[[nodiscard]] auto to_string(Level level) -> std::string_view;
[[nodiscard]] auto to_string(Category category) -> std::string_view;

namespace detail
{

// A format string that also records where it was written.
template <typename... Args>
struct Format
{
    template <typename T>
        requires std::convertible_to<const T &, std::string_view>
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions): a string literal must convert.
    consteval Format(const T &format_string, std::source_location source = std::source_location::current())
        : format{format_string}
        , location{source}
    {
    }

    std::format_string<Args...> format;
    std::source_location location;
};

auto write(
    Level level,
    Category category,
    const std::source_location &location,
    std::string_view format,
    std::format_args args) -> void;

}

template <typename... Args>
auto debug(Category category, detail::Format<std::type_identity_t<Args>...> format, Args &&...args) -> void
{
    if (Level::debug >= compiled_level)
    {
        detail::write(Level::debug, category, format.location, format.format.get(), std::make_format_args(args...));
    }
}

template <typename... Args>
auto info(Category category, detail::Format<std::type_identity_t<Args>...> format, Args &&...args) -> void
{
    if (Level::info >= compiled_level)
    {
        detail::write(Level::info, category, format.location, format.format.get(), std::make_format_args(args...));
    }
}

template <typename... Args>
auto warning(Category category, detail::Format<std::type_identity_t<Args>...> format, Args &&...args) -> void
{
    if (Level::warning >= compiled_level)
    {
        detail::write(Level::warning, category, format.location, format.format.get(), std::make_format_args(args...));
    }
}

template <typename... Args>
auto error(Category category, detail::Format<std::type_identity_t<Args>...> format, Args &&...args) -> void
{
    if (Level::error >= compiled_level)
    {
        detail::write(Level::error, category, format.location, format.format.get(), std::make_format_args(args...));
    }
}

}
