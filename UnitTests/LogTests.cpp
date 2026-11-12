// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Core/Log.h"

#include <doctest.h>

#include <array>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

using namespace primal;

// NOTE: the log is global. Every test adds a sink of its own and removes it, turns the console sink off while it
//       logs (the messages would end up in the test output) and leaves the logger thread stopped.

TEST_SUITE_BEGIN("log");

namespace
{

// Collects what a sink receives.
struct Collector
{
    std::mutex mutex;
    std::vector<log::StoredMessage> messages;

    static auto receive(const log::Message &message, void *user_data) -> void
    {
        auto *const self{static_cast<Collector *>(user_data)};
        const std::scoped_lock lock{self->mutex};
        self->messages.push_back({
            .sequence = message.sequence,
            .time_ns = message.time_ns,
            .text = std::string{message.text},
            .file = message.file,
            .line = message.line,
            .thread = message.thread,
            .level = message.level,
            .category = message.category,
        });
    }

    [[nodiscard]] auto count() -> size_t
    {
        const std::scoped_lock lock{mutex};
        return messages.size();
    }
};

// Sets the log up for one test and restores it afterwards.
class LogTest
{
  public:
    explicit LogTest(bool with_thread)
        : _sink_id{log::add_sink(&Collector::receive, &collector)}
        , _level{log::level()}
    {
        log::enable_console(false);
        if (with_thread)
        {
            log::initialize();
        }
    }

    ~LogTest()
    {
        log::shutdown();
        log::remove_sink(_sink_id);
        log::set_level(_level);
        log::enable_console(true);
    }

    LogTest(const LogTest &) = delete;
    auto operator=(const LogTest &) -> LogTest & = delete;
    LogTest(LogTest &&) = delete;
    auto operator=(LogTest &&) -> LogTest & = delete;

    Collector collector;

  private:
    u32 _sink_id;
    log::Level _level;
};

}

TEST_CASE("a message reaches the sinks with its level, category, text and location")
{
    LogTest test{false};

    log::info(log::Category::content, "Loaded {} entities in {:.1f} ms", 12, 3.25);
    const u32 line{std::source_location::current().line() + 1};
    log::error(log::Category::vulkan, "No arguments");

    REQUIRE(test.collector.messages.size() == 2);
    const log::StoredMessage &first{test.collector.messages[0]};
    CHECK(first.level == log::Level::info);
    CHECK(first.category == log::Category::content);
    CHECK(first.text == "Loaded 12 entities in 3.2 ms");

    const log::StoredMessage &second{test.collector.messages[1]};
    CHECK(second.level == log::Level::error);
    CHECK(second.category == log::Category::vulkan);
    CHECK(second.text == "No arguments");
    CHECK(second.line == line);
    REQUIRE(second.file != nullptr);
    CHECK(std::string_view{second.file}.ends_with("LogTests.cpp"));

    CHECK(second.sequence == first.sequence + 1);
    CHECK(second.time_ns >= first.time_ns);
    CHECK(second.thread == first.thread);
}

TEST_CASE("arguments can be lvalues, rvalues and strings")
{
    LogTest test{false};

    const std::string name{"cube"};
    u32 count{3};
    log::warning(log::Category::tools, "{} has {} LODs ({})", name, count, std::string_view{"ok"});
    ++count;
    log::warning(log::Category::tools, "{}{}", std::string{"temp"}, count);

    REQUIRE(test.collector.messages.size() == 2);
    CHECK(test.collector.messages[0].text == "cube has 3 LODs (ok)");
    CHECK(test.collector.messages[1].text == "temp4");
}

TEST_CASE("a message that is too long is cut and marked")
{
    LogTest test{false};

    const std::string long_text(log::max_message_length * 2, 'x');
    log::info(log::Category::core, "{}", long_text);
    const std::string fits(log::max_message_length, 'y');
    log::info(log::Category::core, "{}", fits);

    REQUIRE(test.collector.messages.size() == 2);
    const std::string &cut{test.collector.messages[0].text};
    CHECK(cut.size() == log::max_message_length);
    CHECK(cut.starts_with("xxxx"));
    CHECK(cut.ends_with("x [...]"));
    CHECK(test.collector.messages[1].text == fits);
}

TEST_CASE("the run-time level filters messages, the compile-time level removes them")
{
    LogTest test{false};

    log::set_level(log::Level::warning);
    CHECK(log::level() == log::Level::warning);
    log::debug(log::Category::core, "debug");
    log::info(log::Category::core, "info");
    log::warning(log::Category::core, "warning");
    log::error(log::Category::core, "error");
    REQUIRE(test.collector.messages.size() == 2);
    CHECK(test.collector.messages[0].text == "warning");
    CHECK(test.collector.messages[1].text == "error");

    test.collector.messages.clear();
    log::set_level(log::Level::debug);
    log::debug(log::Category::core, "debug");
    log::info(log::Category::core, "info");
    // Debug messages exist in Debug builds only (unless PRIMAL_LOG_LEVEL says otherwise).
    const size_t expected{log::compiled_level <= log::Level::debug ? 2U : 1U};
    CHECK(test.collector.messages.size() == expected);
    CHECK(test.collector.messages.back().text == "info");
}

TEST_CASE("the ring buffer keeps messages and returns those after a sequence")
{
    const LogTest test{false};

    std::vector<log::StoredMessage> recent;
    // Earlier tests have left messages there: start after the newest one.
    const u64 start{log::read_recent(0, recent)};
    recent.clear();

    log::info(log::Category::input, "one");
    log::warning(log::Category::input, "two");
    log::info(log::Category::input, "three");

    const u64 newest{log::read_recent(start, recent)};
    REQUIRE(recent.size() == 3);
    CHECK(recent[0].text == "one");
    CHECK(recent[1].text == "two");
    CHECK(recent[1].level == log::Level::warning);
    CHECK(recent[2].text == "three");
    CHECK(recent[2].category == log::Category::input);
    CHECK(newest == recent[2].sequence);

    recent.clear();
    CHECK(log::read_recent(newest - 1, recent) == newest);
    REQUIRE(recent.size() == 1);
    CHECK(recent[0].text == "three");

    recent.clear();
    CHECK(log::read_recent(newest, recent) == newest);
    CHECK(recent.empty());
}

TEST_CASE("a removed sink gets nothing more")
{
    const LogTest test{false};
    Collector other;
    const u32 id{log::add_sink(&Collector::receive, &other)};

    log::info(log::Category::core, "both");
    log::remove_sink(id);
    log::info(log::Category::core, "one");

    CHECK(other.messages.size() == 1);
    CHECK(test.collector.messages.size() == 2);
}

TEST_CASE("level and category names")
{
    CHECK(log::to_string(log::Level::debug) == "debug");
    CHECK(log::to_string(log::Level::error) == "error");
    CHECK(log::to_string(log::Category::core) == "core");
    CHECK(log::to_string(log::Category::tools) == "tools");
    CHECK(log::to_string(log::Category::test) == "test");
    CHECK(log::to_string(log::Category::count) == "?");
}

TEST_CASE("the file sink writes every message and keeps the previous file")
{
    const LogTest test{false};
    const std::filesystem::path folder{std::filesystem::temp_directory_path() / "primal_log_test"};
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::filesystem::path path{folder / "test.log"};
    const std::filesystem::path previous{folder / "test.1.log"};

    const auto read{
        [](const std::filesystem::path &file) -> std::string
        {
            std::ifstream stream{file, std::ios::binary};
            return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
        }};

    REQUIRE(log::open_file(path.string().c_str()));
    log::info(log::Category::content, "first run");
    log::warning(log::Category::content, "with a location");
    log::close_file();
    log::info(log::Category::content, "not in the file");

    const std::string first{read(path)};
    CHECK(first.find("[info   ] [content] first run\n") != std::string::npos);
    CHECK(first.find("[warning] [content] with a location (LogTests.cpp:") != std::string::npos);
    CHECK(first.find("not in the file") == std::string::npos);
    CHECK_FALSE(std::filesystem::exists(previous));

    REQUIRE(log::open_file(path.string().c_str()));
    log::info(log::Category::content, "second run");
    log::close_file();

    CHECK(read(previous) == first);
    const std::string second{read(path)};
    CHECK(second.find("second run") != std::string::npos);
    CHECK(second.find("first run") == std::string::npos);

    CHECK_FALSE(log::open_file((folder / "no_such_folder" / "test.log").string().c_str()));
    std::filesystem::remove_all(folder);
}

TEST_CASE("with the logger thread: flush() and error() wait for the sinks")
{
    LogTest test{true};

    for (u32 i{0}; i < 10; ++i)
    {
        log::info(log::Category::core, "message {}", i);
    }
    log::flush();
    CHECK(test.collector.count() == 10);

    // error() has delivered its message, and everything before it, when it returns.
    log::info(log::Category::core, "before the error");
    log::error(log::Category::core, "error");
    CHECK(test.collector.count() == 12);

    // initialize() twice is harmless; shutdown() writes what is queued.
    log::initialize();
    for (u32 i{0}; i < 200; ++i)
    {
        log::info(log::Category::core, "queued {}", i);
    }
    log::shutdown();
    CHECK(test.collector.count() == 212);
    CHECK(test.collector.messages.back().text == "queued 199");

    // Without the thread, the caller writes.
    log::info(log::Category::core, "after shutdown");
    CHECK(test.collector.count() == 213);
    log::shutdown();
    log::flush();
}

TEST_CASE("many threads: nothing is lost, sequences rise, each thread's messages stay in order")
{
    constexpr u32 thread_count{8};
    constexpr u32 messages_per_thread{500};

    const LogTest test{true};
    std::atomic<bool> go{false};
    std::array<std::thread, thread_count> threads;
    for (u32 t{0}; t < thread_count; ++t)
    {
        threads[t] = std::thread{[t, &go]
                                 {
                                     while (!go.load())
                                     {
                                         std::this_thread::yield();
                                     }
                                     for (u32 i{0}; i < messages_per_thread; ++i)
                                     {
                                         if (i % 100 == 99)
                                         {
                                             log::error(log::Category::script, "{} {}", t, i);
                                         }
                                         else
                                         {
                                             log::info(log::Category::script, "{} {}", t, i);
                                         }
                                     }
                                 }};
    }

    go.store(true);
    // The logger thread is stopped and started while the others log: messages must survive both ways of writing.
    log::shutdown();
    log::initialize();
    for (std::thread &thread : threads)
    {
        thread.join();
    }
    log::flush();

    const std::vector<log::StoredMessage> &messages{test.collector.messages};
    REQUIRE(messages.size() == thread_count * messages_per_thread);

    std::array<u32, thread_count> next{};
    u32 out_of_order{0};
    u32 bad_sequence{0};
    for (size_t i{0}; i < messages.size(); ++i)
    {
        // The text is "<thread> <index>".
        const std::string &text{messages[i].text};
        const size_t space{text.find(' ')};
        REQUIRE(space != std::string::npos);
        u32 thread{0};
        u32 index{0};
        REQUIRE(std::from_chars(text.data(), text.data() + space, thread).ec == std::errc{});
        REQUIRE(std::from_chars(text.data() + space + 1, text.data() + text.size(), index).ec == std::errc{});
        REQUIRE(thread < thread_count);
        out_of_order += (index != next[thread]) ? 1U : 0U;
        next[thread] = index + 1;
        bad_sequence += (i > 0 && messages[i].sequence != messages[i - 1].sequence + 1) ? 1U : 0U;
    }
    CHECK(out_of_order == 0);
    CHECK(bad_sequence == 0);
    for (const u32 count : next)
    {
        CHECK(count == messages_per_thread);
    }
}

TEST_SUITE_END();
