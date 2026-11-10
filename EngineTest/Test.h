// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "../Engine/Common/PlatformDetection.h"

#include <chrono>
#include <string>
#include <thread>

#ifndef TEST_ENTITY_COMPONENTS
#define TEST_ENTITY_COMPONENTS 0
#endif
#ifndef TEST_WINDOW
#define TEST_WINDOW 0
#endif
#ifndef TEST_RENDERER
#define TEST_RENDERER 1
#endif
#ifndef TEST_SURFACES
#define TEST_SURFACES 0
#endif

class Test
{
  public:
    virtual auto initialize() -> bool = 0;
    virtual auto run() -> void = 0;
    virtual auto shutdown() -> void = 0;
};

#if PRIMAL_PLATFORM_WINDOWS
// Lean: the full Windows.h pulls in winsock, whose shutdown() clashes with the tests' own.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <cstdio>
#endif
class TimeIt
{
  public:
    using clock = std::chrono::steady_clock;
    using time_stamp = clock::time_point;

    // Average frame time per second.
    constexpr auto dt_avg() const -> float
    {
        return _dt_avg * 1e-6f;
    }

    auto begin() -> void
    {
        _start = clock::now();
    }

    auto end() -> void
    {
        const auto dt = clock::now() - _start;
        _us_avg += (static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(dt).count()) - _us_avg) /
                   static_cast<float>(_counter);
        ++_counter;
        _dt_avg = _us_avg;

        if (std::chrono::duration_cast<std::chrono::seconds>(clock::now() - _seconds).count() >= 1)
        {
            const std::string msg{
                "Avg. frame (ms): " + std::to_string(_us_avg * 0.001f) + " " + std::to_string(_counter) + " fps\n"};
#if PRIMAL_PLATFORM_WINDOWS
            ::OutputDebugStringA(msg.c_str());
#else
            std::fputs(msg.c_str(), stderr);
#endif
            _us_avg = 0.f;
            _counter = 1;
            _seconds = clock::now();
        }
    }

  private:
    float _dt_avg{16.7f};
    float _us_avg{0.f};
    int _counter{1};
    time_stamp _start;
    time_stamp _seconds{clock::now()};
};
