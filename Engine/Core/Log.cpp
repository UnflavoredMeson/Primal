// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Log.h"

#include "../Common/PlatformDetection.h"

#if PRIMAL_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

// MSVC's <chrono> has a try block (the time zone database), which warns (C4530) when the engine is built without
// exceptions (Engine.vcxproj). The engine doesn't use time zones.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4530)
#endif
#include <chrono>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#define PRIMAL_LOG_HAS_EXCEPTIONS 1
#else
#define PRIMAL_LOG_HAS_EXCEPTIONS 0
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <thread>

namespace primal::log
{

namespace
{

constexpr u32 queue_capacity{64};
constexpr u32 ring_capacity{2048};
constexpr std::string_view cut_mark{" [...]"};

// A message with its own storage, as it waits in the queue.
struct Record
{
    u64 sequence{0};
    u64 time_ns{0};
    const char *file{nullptr};
    u32 line{0};
    u32 thread{0};
    u32 length{0};
    Level level{Level::info};
    Category category{Category::core};
    std::array<char, max_message_length> text;

    [[nodiscard]] auto message() const -> Message
    {
        return {
            .sequence = sequence,
            .time_ns = time_ns,
            .text = std::string_view{text.data(), length},
            .file = file,
            .line = line,
            .thread = thread,
            .level = level,
            .category = category,
        };
    }
};

struct Sink
{
    sink_function function{nullptr};
    void *user_data{nullptr};
    u32 id{0};
};

struct State
{
    // Guards the queue and the sequence numbers.
    std::mutex queue_mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
    std::condition_variable delivered_changed;
    std::unique_ptr<Record[]> queue{std::make_unique<Record[]>(queue_capacity)};
    u32 queue_head{0};
    u32 queue_count{0};
    u64 last_sequence{0};
    // Every message up to this sequence has reached the sinks.
    u64 delivered_sequence{0};
    bool thread_running{false};
    bool stop_requested{false};
    std::thread thread;

    // Guards the sinks and the ring buffer. Lock order: queue_mutex first, when both are held.
    std::mutex sink_mutex;
    std::vector<Sink> sinks;
    std::deque<StoredMessage> ring;
    u32 next_sink_id{1};
    bool console_enabled{true};
    bool console_is_terminal{false};
    ConsoleStyle console_style{ConsoleStyle::full};
    std::FILE *file{nullptr};

    // Copies of the file names of forwarded messages (detail::write_forwarded()): the names belong to a game DLL
    // that may be unloaded while the ring buffer still points at them. Never shrinks.
    std::mutex forwarded_files_mutex;
    std::set<std::string, std::less<>> forwarded_files;

    std::atomic<Level> level{compiled_level};
    std::atomic<u32> thread_count{0};
    std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};

    State();
    ~State();
    State(const State &) = delete;
    auto operator=(const State &) -> State & = delete;
    State(State &&) = delete;
    auto operator=(State &&) -> State & = delete;
};

// Set in a game DLL's copy of the log: where its messages go instead (detail::set_forward()).
std::atomic<detail::forward_function> forward{nullptr};

// Set when the State is destroyed, as the process ends. Static objects that are destroyed later may still log:
// their messages go straight to stderr.
std::atomic<bool> state_destroyed{false};

auto state() -> State &
{
    static State instance;
    return instance;
}

// Set while this thread is inside a sink, to catch a sink that logs.
thread_local bool in_sink{false};

auto thread_number() -> u32
{
    thread_local const u32 number{state().thread_count.fetch_add(1, std::memory_order_relaxed)};
    return number;
}

// An output iterator that writes into a Record and drops what doesn't fit.
class RecordWriter
{
  public:
    using difference_type = std::ptrdiff_t;

    explicit RecordWriter(Record &record)
        : _record{&record}
    {
    }

    auto operator=(char character) -> RecordWriter &
    {
        if (_record->length < max_message_length)
        {
            _record->text[_record->length] = character;
        }
        // Counts on, so that the caller can tell that the text was cut.
        ++_record->length;
        return *this;
    }

    auto operator*() -> RecordWriter &
    {
        return *this;
    }

    auto operator++() -> RecordWriter &
    {
        return *this;
    }

    auto operator++(int) -> RecordWriter
    {
        return *this;
    }

  private:
    Record *_record;
};

auto format_text(Record &record, std::string_view format, std::format_args args) -> void
{
    record.length = 0;
#if PRIMAL_LOG_HAS_EXCEPTIONS
    try
    {
        std::vformat_to(RecordWriter{record}, format, args);
    }
    catch (const std::exception &exception)
    {
        record.length = 0;
        std::format_to(RecordWriter{record}, "(message could not be formatted: {})", exception.what());
    }
#else
    // The engine builds without exceptions on MSVC (Engine.vcxproj). The format string was checked at compile time
    // (log::detail::Format), so only a bad runtime width or precision can still fail, and that ends the program.
    std::vformat_to(RecordWriter{record}, format, args);
#endif

    if (record.length > max_message_length)
    {
        std::memcpy(&record.text[max_message_length - cut_mark.size()], cut_mark.data(), cut_mark.size());
        record.length = max_message_length;
    }
}

auto color_of(Level level) -> std::string_view
{
    switch (level)
    {
        case Level::debug: return "\x1b[2m";
        case Level::warning: return "\x1b[33m";
        case Level::error: return "\x1b[31m";
        case Level::info: break;
    }
    return "";
}

// The file name without its folders.
auto short_file_name(const char *file) -> std::string_view
{
    const std::string_view path{file ? file : ""};
    const size_t separator{path.find_last_of("/\\")};
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

// Writes "[time] [level] [category] text (file:line)" and a line end into 'line'. Returns the text written.
auto format_line(std::span<char> line, const Message &message, bool colored, ConsoleStyle style) -> std::string_view
{
    const std::string_view color_on{colored ? color_of(message.level) : std::string_view{}};
    const std::string_view color_off{colored ? std::string_view{"\x1b[0m"} : std::string_view{}};
    // One character is left for a terminator.
    const auto size{static_cast<std::ptrdiff_t>(line.size() - 1)};

    if (style == ConsoleStyle::plain)
    {
        const auto result{std::format_to_n(line.data(), size, "{}{}{}\n", color_on, message.text, color_off)};
        return {line.data(), static_cast<size_t>(result.out - line.data())};
    }

    const double seconds{static_cast<double>(message.time_ns) * 1e-9};
    const bool has_location{message.level >= Level::warning && message.file};
    const std::string location{
        has_location ? std::format(" ({}:{})", short_file_name(message.file), message.line) : std::string{}};

    const auto result{std::format_to_n(
        line.data(),
        size,
        "{}[{:10.4f}] [{:<7}] [{}] {}{}{}\n",
        color_on,
        seconds,
        to_string(message.level),
        to_string(message.category),
        message.text,
        location,
        color_off)};
    return {line.data(), static_cast<size_t>(result.out - line.data())};
}

auto write_to_console(const State &current, const Message &message) -> void
{
    // The whole line goes out in one write, so that other writers to stderr don't end up inside it.
    std::array<char, max_message_length + 512> line;
    const bool colored{current.console_is_terminal && message.level != Level::info};
    const std::string_view text{format_line(line, message, colored, current.console_style)};
    std::fwrite(text.data(), 1, text.size(), stderr);
#if PRIMAL_PLATFORM_WINDOWS
    // Visible in a debugger's output window. (No colors on Windows: console_is_terminal is never set.)
    line[text.size()] = '\0';
    ::OutputDebugStringA(line.data());
#endif
}

auto write_to_file(const State &current, const Message &message) -> void
{
    std::array<char, max_message_length + 512> line;
    const std::string_view text{format_line(line, message, false, ConsoleStyle::full)};
    std::fwrite(text.data(), 1, text.size(), current.file);
    if (message.level >= Level::warning)
    {
        std::fflush(current.file);
    }
}

// Gives one message to the ring buffer and to every sink.
auto deliver(State &current, const Record &record) -> void
{
    const Message message{record.message()};
    const std::scoped_lock lock{current.sink_mutex};

    if (current.ring.size() == ring_capacity)
    {
        current.ring.pop_front();
    }
    current.ring.push_back({
        .sequence = message.sequence,
        .time_ns = message.time_ns,
        .text = std::string{message.text},
        .file = message.file,
        .line = message.line,
        .thread = message.thread,
        .level = message.level,
        .category = message.category,
    });

    in_sink = true;
    if (current.console_enabled)
    {
        write_to_console(current, message);
    }
    if (current.file)
    {
        write_to_file(current, message);
    }
    for (const Sink &sink : current.sinks)
    {
        sink.function(message, sink.user_data);
    }
    in_sink = false;
}

auto flush_outputs(State &current) -> void
{
    std::fflush(stderr);
    const std::scoped_lock lock{current.sink_mutex};
    if (current.file)
    {
        std::fflush(current.file);
    }
}

// Lets the logger thread write what is queued, and waits until it has ended.
auto stop_logger_thread(State &current) -> void
{
    std::thread thread;
    {
        const std::scoped_lock lock{current.queue_mutex};
        if (!current.thread_running || current.stop_requested)
        {
            return;
        }
        // From here on, callers write their messages themselves. They wait in write() until the queue is empty,
        // so the order of the messages is kept.
        current.stop_requested = true;
        thread = std::move(current.thread);
    }

    current.not_empty.notify_all();
    thread.join();

    {
        const std::scoped_lock lock{current.queue_mutex};
        current.thread_running = false;
    }
    current.delivered_changed.notify_all();
    flush_outputs(current);
}

auto logger_thread() -> void
{
    State &current{state()};
    // Not on the stack of every caller: a Record is 2 KiB, and this one lives as long as the thread.
    const auto record{std::make_unique<Record>()};

    std::unique_lock lock{current.queue_mutex};
    while (true)
    {
        current.not_empty.wait(lock, [&current] { return current.queue_count || current.stop_requested; });
        if (!current.queue_count)
        {
            // Stop was requested and everything has been written.
            return;
        }

        *record = current.queue[current.queue_head];
        current.queue_head = (current.queue_head + 1) % queue_capacity;
        --current.queue_count;
        current.not_full.notify_one();

        lock.unlock();
        deliver(current, *record);
        lock.lock();

        current.delivered_sequence = record->sequence;
        current.delivered_changed.notify_all();
    }
}

State::State()
{
#if PRIMAL_PLATFORM_LINUX
    console_is_terminal = ::isatty(STDERR_FILENO) != 0 && std::getenv("NO_COLOR") == nullptr;
#endif
}

State::~State()
{
    // The process is ending without log::shutdown(): write what is queued.
    stop_logger_thread(*this);
    if (file)
    {
        std::fclose(file);
    }
    state_destroyed.store(true);
}

}

auto initialize() -> void
{
    if (state_destroyed.load())
    {
        return;
    }

    State &current{state()};
    const std::scoped_lock lock{current.queue_mutex};
    if (current.thread_running)
    {
        return;
    }

    current.stop_requested = false;
    current.thread_running = true;
    current.thread = std::thread{logger_thread};
}

auto shutdown() -> void
{
    if (!state_destroyed.load())
    {
        stop_logger_thread(state());
    }
}

auto flush() -> void
{
    if (state_destroyed.load())
    {
        return;
    }

    State &current{state()};
    {
        std::unique_lock lock{current.queue_mutex};
        const u64 target{current.last_sequence};
        current.delivered_changed.wait(lock, [&current, target] { return current.delivered_sequence >= target; });
    }
    flush_outputs(current);
}

auto set_level(Level level) -> void
{
    if (state_destroyed.load())
    {
        return;
    }

    state().level.store(level, std::memory_order_relaxed);
}

auto level() -> Level
{
    if (state_destroyed.load())
    {
        return compiled_level;
    }

    return state().level.load(std::memory_order_relaxed);
}

auto add_sink(sink_function function, void *user_data) -> u32
{
    if (state_destroyed.load())
    {
        return 0;
    }

    State &current{state()};
    const std::scoped_lock lock{current.sink_mutex};
    const u32 id{current.next_sink_id++};
    current.sinks.push_back({.function = function, .user_data = user_data, .id = id});
    return id;
}

auto remove_sink(u32 sink_id) -> void
{
    if (state_destroyed.load())
    {
        return;
    }

    State &current{state()};
    const std::scoped_lock lock{current.sink_mutex};
    std::erase_if(current.sinks, [sink_id](const Sink &sink) { return sink.id == sink_id; });
}

auto enable_console(bool enabled) -> void
{
    if (state_destroyed.load())
    {
        return;
    }

    State &current{state()};
    const std::scoped_lock lock{current.sink_mutex};
    current.console_enabled = enabled;
}

auto set_console_style(ConsoleStyle style) -> void
{
    if (state_destroyed.load())
    {
        return;
    }

    State &current{state()};
    const std::scoped_lock lock{current.sink_mutex};
    current.console_style = style;
}

auto open_file(const char *utf8_path) -> bool
{
    if (state_destroyed.load())
    {
        return false;
    }

    close_file();

    // The path is UTF-8 on every platform (a char path would be the ANSI code page on Windows).
    const std::string_view utf8{utf8_path};
    const std::filesystem::path path{std::u8string{utf8.begin(), utf8.end()}};
    std::error_code error;
    if (std::filesystem::exists(path, error))
    {
        std::filesystem::path previous{path};
        previous.replace_extension();
        previous += ".1";
        previous += path.extension();
        std::filesystem::rename(path, previous, error);
    }

#if PRIMAL_PLATFORM_WINDOWS
    std::FILE *file{nullptr};
    if (::_wfopen_s(&file, path.c_str(), L"wb") != 0)
    {
        file = nullptr;
    }
#else
    std::FILE *const file{std::fopen(path.c_str(), "wb")};
#endif
    if (!file)
    {
        return false;
    }

    State &current{state()};
    const std::scoped_lock lock{current.sink_mutex};
    current.file = file;
    return true;
}

auto close_file() -> void
{
    if (state_destroyed.load())
    {
        return;
    }

    // What was logged so far belongs in the file.
    flush();

    State &current{state()};
    const std::scoped_lock lock{current.sink_mutex};
    if (current.file)
    {
        std::fclose(current.file);
        current.file = nullptr;
    }
}

auto read_recent(u64 after_sequence, std::vector<StoredMessage> &messages) -> u64
{
    if (state_destroyed.load())
    {
        return after_sequence;
    }

    State &current{state()};
    const std::scoped_lock lock{current.sink_mutex};
    for (const StoredMessage &message : current.ring)
    {
        if (message.sequence > after_sequence)
        {
            messages.push_back(message);
        }
    }
    return current.ring.empty() ? after_sequence : std::max(after_sequence, current.ring.back().sequence);
}

auto to_string(Level level) -> std::string_view
{
    switch (level)
    {
        case Level::debug: return "debug";
        case Level::info: return "info";
        case Level::warning: return "warning";
        case Level::error: return "error";
    }
    return "?";
}

auto to_string(Category category) -> std::string_view
{
    constexpr std::array<std::string_view, static_cast<size_t>(Category::count)> names{
        "core", "graphics", "vulkan", "d3d12", "content", "input", "script", "tools", "test"};
    const auto index{static_cast<size_t>(category)};
    return index < names.size() ? names[index] : "?";
}

namespace detail
{

namespace
{

auto write_after_shutdown(const Record &record) -> void
{
    const std::string_view name{to_string(record.level)};
    std::fprintf(
        stderr,
        "[%.*s] %.*s\n",
        static_cast<int>(name.size()),
        name.data(),
        static_cast<int>(record.length),
        record.text.data());
}

// Hands a formatted record (text, file, line, level, category set) to the logger thread or the sinks.
auto submit(State &current, Record &record) -> void
{
    record.time_ns = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - current.start_time)
            .count());
    record.thread = thread_number();
    const Level level{record.level};

    if (in_sink)
    {
        // A sink that logs would wait for itself. Its message goes to stderr only.
        std::fprintf(stderr, "[log] (from a sink) %.*s\n", static_cast<int>(record.length), record.text.data());
        return;
    }

    std::unique_lock lock{current.queue_mutex};
    if (current.thread_running && !current.stop_requested)
    {
        current.not_full.wait(lock, [&current] { return current.queue_count < queue_capacity; });
    }

    if (current.thread_running && !current.stop_requested)
    {
        record.sequence = ++current.last_sequence;
        current.queue[(current.queue_head + current.queue_count) % queue_capacity] = record;
        ++current.queue_count;
        current.not_empty.notify_one();

        if (level == Level::error)
        {
            const u64 sequence{record.sequence};
            current.delivered_changed.wait(
                lock, [&current, sequence] { return current.delivered_sequence >= sequence; });
            lock.unlock();
            flush_outputs(current);
        }
        return;
    }

    // No logger thread (not started, or stopping): this thread writes the message. While the logger thread is
    // stopping, wait until it has written what was queued, to keep the order.
    current.delivered_changed.wait(
        lock, [&current] { return !current.thread_running || current.delivered_sequence == current.last_sequence; });
    record.sequence = ++current.last_sequence;
    // Delivered under the queue lock, so that messages written this way reach the sinks in sequence order.
    deliver(current, record);
    current.delivered_sequence = record.sequence;
    current.delivered_changed.notify_all();
}

}

auto write(
    Level level,
    Category category,
    const std::source_location &location,
    std::string_view format,
    std::format_args args) -> void
{
    Record record;
    record.file = location.file_name();
    record.line = location.line();
    record.level = level;
    record.category = category;

    if (const forward_function function{forward.load(std::memory_order_acquire)})
    {
        // A game DLL's copy: the receiving log checks the level.
        format_text(record, format, args);
        function(level, category, record.file, record.line, record.text.data(), record.length);
        return;
    }

    if (state_destroyed.load())
    {
        format_text(record, format, args);
        write_after_shutdown(record);
        return;
    }

    State &current{state()};
    if (level < current.level.load(std::memory_order_relaxed))
    {
        return;
    }

    format_text(record, format, args);
    submit(current, record);
}

auto set_forward(forward_function function) -> void
{
    forward.store(function, std::memory_order_release);
}

auto write_forwarded(Level level, Category category, const char *file, u32 line, const char *text, u32 length)
    -> void
{
    Record record;
    record.line = line;
    record.level = level;
    record.category = category;
    record.length = std::min(length, max_message_length);
    std::memcpy(record.text.data(), text, record.length);

    if (state_destroyed.load())
    {
        write_after_shutdown(record);
        return;
    }

    State &current{state()};
    if (level < current.level.load(std::memory_order_relaxed))
    {
        return;
    }

    if (file)
    {
        const std::scoped_lock lock{current.forwarded_files_mutex};
        record.file = current.forwarded_files.emplace(file).first->c_str();
    }
    submit(current, record);
}

}

}
