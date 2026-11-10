// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "Input/Input.h"

#include <doctest.h>

using namespace primal;

// NOTE: the input system is global and keeps its state between test cases. Every test uses binding keys of its own,
//       starts from released keys (reset()) and removes its bindings.

TEST_SUITE_BEGIN("input");

namespace
{

using input::Axis;
using input::InputCode;
using input::InputSource;
using input::InputValue;

constexpr math::v3 pressed{1.f, 0.f, 0.f};
constexpr math::v3 released{0.f, 0.f, 0.f};

// Sets current and previous to zero.
auto reset(InputSource::Type type, InputCode::Code code) -> void
{
    input::set(type, code, released);
    input::set(type, code, released);
}

auto key_source(u64 binding, InputCode::Code code, f32 multiplier, Axis::Type axis = Axis::x) -> InputSource
{
    return {
        .binding = binding,
        .source_type = InputSource::keyboard,
        .code = code,
        .multiplier = multiplier,
        .source_axis = Axis::x,
        .axis = axis,
    };
}

auto binding_value(u64 binding) -> InputValue
{
    InputValue value{};
    input::get(binding, value);
    return value;
}

struct Listener
{
    auto on_input(InputSource::Type type, InputCode::Code code, const InputValue &value) -> void
    {
        ++input_events;
        last_type = type;
        last_code = code;
        last_input = value;
    }

    auto on_binding(u64 binding, const InputValue &value) -> void
    {
        ++binding_events;
        last_binding = binding;
        last_binding_value = value;
    }

    u32 input_events{0};
    u32 binding_events{0};
    InputSource::Type last_type{InputSource::count};
    InputCode::Code last_code{};
    u64 last_binding{0};
    InputValue last_input{};
    InputValue last_binding_value{};
};

}

TEST_CASE("set keeps the current and the previous value of an input")
{
    reset(InputSource::keyboard, InputCode::key_a);

    input::set(InputSource::keyboard, InputCode::key_a, pressed);
    InputValue value{};
    input::get(InputSource::keyboard, InputCode::key_a, value);
    CHECK(value.current.x == 1.f);
    CHECK(value.previous.x == 0.f);

    input::set(InputSource::keyboard, InputCode::key_a, released);
    input::get(InputSource::keyboard, InputCode::key_a, value);
    CHECK(value.current.x == 0.f);
    CHECK(value.previous.x == 1.f);
}

TEST_CASE("the same code on different source types is a different input")
{
    reset(InputSource::keyboard, InputCode::key_w);
    reset(InputSource::controller, InputCode::key_w);

    input::set(InputSource::keyboard, InputCode::key_w, pressed);

    InputValue value{};
    input::get(InputSource::controller, InputCode::key_w, value);
    CHECK(value.current.x == 0.f);

    reset(InputSource::keyboard, InputCode::key_w);
}

TEST_CASE("a binding adds up its sources, each times its multiplier")
{
    constexpr u64 binding{0x7e57'0001};
    reset(InputSource::keyboard, InputCode::key_a);
    reset(InputSource::keyboard, InputCode::key_d);
    input::bind(key_source(binding, InputCode::key_a, -1.f));
    input::bind(key_source(binding, InputCode::key_d, 1.f));

    CHECK(binding_value(binding).current.x == 0.f);

    input::set(InputSource::keyboard, InputCode::key_d, pressed);
    CHECK(binding_value(binding).current.x == 1.f);

    // Both directions held: they cancel.
    input::set(InputSource::keyboard, InputCode::key_a, pressed);
    CHECK(binding_value(binding).current.x == 0.f);

    input::set(InputSource::keyboard, InputCode::key_d, released);
    const InputValue value{binding_value(binding)};
    CHECK(value.current.x == -1.f);
    // Previous is the sum of the sources' previous values: d was down (1), a was up before its only press (0).
    CHECK(value.previous.x == 1.f);

    input::unbind(binding);
    reset(InputSource::keyboard, InputCode::key_a);
    reset(InputSource::keyboard, InputCode::key_d);
}

TEST_CASE("a source writes to the axis it is bound to")
{
    constexpr u64 binding{0x7e57'0002};
    reset(InputSource::keyboard, InputCode::key_w);
    reset(InputSource::keyboard, InputCode::key_d);
    input::bind(key_source(binding, InputCode::key_d, 1.f, Axis::x));
    input::bind(key_source(binding, InputCode::key_w, 2.f, Axis::z));

    input::set(InputSource::keyboard, InputCode::key_d, pressed);
    input::set(InputSource::keyboard, InputCode::key_w, pressed);

    const InputValue value{binding_value(binding)};
    CHECK(value.current.x == 1.f);
    CHECK(value.current.y == 0.f);
    CHECK(value.current.z == 2.f);

    input::unbind(binding);
    reset(InputSource::keyboard, InputCode::key_w);
    reset(InputSource::keyboard, InputCode::key_d);
}

TEST_CASE("a mouse source gives the movement since the last value")
{
    constexpr u64 binding{0x7e57'0003};
    reset(InputSource::mouse, InputCode::mouse_position);
    input::bind({
        .binding = binding,
        .source_type = InputSource::mouse,
        .code = InputCode::mouse_position,
        .multiplier = 2.f,
        .source_axis = Axis::y,
        .axis = Axis::x,
    });

    input::set(InputSource::mouse, InputCode::mouse_position, {100.f, 50.f, 0.f});
    input::set(InputSource::mouse, InputCode::mouse_position, {130.f, 45.f, 0.f});

    // The y movement (-5) times the multiplier, on the binding's x axis.
    CHECK(binding_value(binding).current.x == -10.f);

    input::unbind(binding);
    reset(InputSource::mouse, InputCode::mouse_position);
}

TEST_CASE("asking a binding twice gives the same value")
{
    constexpr u64 binding{0x7e57'0004};
    reset(InputSource::keyboard, InputCode::key_s);
    input::bind(key_source(binding, InputCode::key_s, 3.f));
    input::set(InputSource::keyboard, InputCode::key_s, pressed);

    CHECK(binding_value(binding).current.x == 3.f);
    CHECK(binding_value(binding).current.x == 3.f);

    input::unbind(binding);
    reset(InputSource::keyboard, InputCode::key_s);
}

TEST_CASE("unbinding")
{
    constexpr u64 binding{0x7e57'0005};
    reset(InputSource::keyboard, InputCode::key_a);
    reset(InputSource::keyboard, InputCode::key_d);
    input::bind(key_source(binding, InputCode::key_a, -1.f));
    input::bind(key_source(binding, InputCode::key_d, 1.f));
    input::set(InputSource::keyboard, InputCode::key_a, pressed);
    input::set(InputSource::keyboard, InputCode::key_d, pressed);

    SUBCASE("one source: the others stay")
    {
        input::unbind(InputSource::keyboard, InputCode::key_a);
        // Any change to a remaining source refreshes the binding.
        input::set(InputSource::keyboard, InputCode::key_d, pressed);
        CHECK(binding_value(binding).current.x == 1.f);
    }

    SUBCASE("the whole binding: get leaves the value alone")
    {
        input::unbind(binding);

        InputValue value{.previous = {5.f, 5.f, 5.f}, .current = {6.f, 6.f, 6.f}};
        input::get(binding, value);
        CHECK(value.current.x == 6.f);
        CHECK(value.previous.x == 5.f);
    }

    SUBCASE("every source one by one removes the binding")
    {
        input::unbind(InputSource::keyboard, InputCode::key_a);
        input::unbind(InputSource::keyboard, InputCode::key_d);

        InputValue value{.previous = {}, .current = {6.f, 6.f, 6.f}};
        input::get(binding, value);
        CHECK(value.current.x == 6.f);
    }

    SUBCASE("something that isn't bound: nothing happens")
    {
        input::unbind(InputSource::keyboard, InputCode::key_w);
        input::unbind(u64{0x7e57'ffff});
        input::set(InputSource::keyboard, InputCode::key_d, pressed);
        CHECK(binding_value(binding).current.x == 0.f);
    }

    input::unbind(binding);
    reset(InputSource::keyboard, InputCode::key_a);
    reset(InputSource::keyboard, InputCode::key_d);
}

TEST_CASE("binding a source again moves it to the new binding")
{
    constexpr u64 old_binding{0x7e57'0006};
    constexpr u64 new_binding{0x7e57'0007};
    reset(InputSource::keyboard, InputCode::key_s);
    input::bind(key_source(old_binding, InputCode::key_s, 1.f));
    input::bind(key_source(new_binding, InputCode::key_s, 4.f));

    input::set(InputSource::keyboard, InputCode::key_s, pressed);

    CHECK(binding_value(new_binding).current.x == 4.f);
    // The old binding lost its only source, so it no longer exists: get leaves the value alone.
    InputValue value{.previous = {}, .current = {9.f, 9.f, 9.f}};
    input::get(old_binding, value);
    CHECK(value.current.x == 9.f);

    input::unbind(new_binding);
    reset(InputSource::keyboard, InputCode::key_s);
}

TEST_CASE("an InputSystem gets the inputs of the source types it handles")
{
    reset(InputSource::keyboard, InputCode::key_w);
    reset(InputSource::mouse, InputCode::mouse_left);
    Listener listener;
    {
        input::InputSystem<Listener> system;
        system.add_handler(InputSource::keyboard, &listener, &Listener::on_input);
        // Adding the same handler again doesn't make it run twice.
        system.add_handler(InputSource::keyboard, &listener, &Listener::on_input);

        input::set(InputSource::keyboard, InputCode::key_w, pressed);
        CHECK(listener.input_events == 1U);
        CHECK(listener.last_type == InputSource::keyboard);
        CHECK(listener.last_code == InputCode::key_w);
        CHECK(listener.last_input.current.x == 1.f);
        CHECK(listener.last_input.previous.x == 0.f);

        input::set(InputSource::mouse, InputCode::mouse_left, pressed);
        CHECK(listener.input_events == 1U);
    }

    // The system is gone: no more calls.
    input::set(InputSource::keyboard, InputCode::key_w, released);
    CHECK(listener.input_events == 1U);

    reset(InputSource::keyboard, InputCode::key_w);
    reset(InputSource::mouse, InputCode::mouse_left);
}

TEST_CASE("an InputSystem gets the value of a binding when one of its sources changes")
{
    constexpr u64 binding{0x7e57'0008};
    constexpr u64 other_binding{0x7e57'0009};
    reset(InputSource::keyboard, InputCode::key_a);
    reset(InputSource::keyboard, InputCode::key_d);
    reset(InputSource::keyboard, InputCode::key_w);
    input::bind(key_source(binding, InputCode::key_a, -1.f));
    input::bind(key_source(binding, InputCode::key_d, 1.f));
    input::bind(key_source(other_binding, InputCode::key_w, 1.f));

    Listener listener;
    {
        input::InputSystem<Listener> system;
        system.add_handler(binding, &listener, &Listener::on_binding);

        input::set(InputSource::keyboard, InputCode::key_d, pressed);
        CHECK(listener.binding_events == 1U);
        CHECK(listener.last_binding == binding);
        CHECK(listener.last_binding_value.current.x == 1.f);

        input::set(InputSource::keyboard, InputCode::key_a, pressed);
        CHECK(listener.binding_events == 2U);
        CHECK(listener.last_binding_value.current.x == 0.f);

        // A source of another binding.
        input::set(InputSource::keyboard, InputCode::key_w, pressed);
        CHECK(listener.binding_events == 2U);
    }

    input::unbind(binding);
    input::unbind(other_binding);
    reset(InputSource::keyboard, InputCode::key_a);
    reset(InputSource::keyboard, InputCode::key_d);
    reset(InputSource::keyboard, InputCode::key_w);
}

TEST_SUITE_END();
