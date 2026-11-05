// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "../Engine/Common/PlatformDetection.h"
#include <thread>
#include <chrono>
#include <string>

#ifndef TEST_ENTITY_COMPONENTS
#define TEST_ENTITY_COMPONENTS 0
#endif
#ifndef TEST_WINDOW
#define TEST_WINDOW 0
#endif
#ifndef TEST_RENDERER
#define TEST_RENDERER 1
#endif

class test
{
public:
    virtual bool initialize() = 0;
    virtual void run() = 0;
    virtual void shutdown() = 0;
};

#if PRIMAL_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <cstdio>
#endif
class time_it
{
public:
    using clock = std::chrono::steady_clock;
    using time_stamp = clock::time_point;

    // Average frame time per second.
    constexpr float dt_avg() const { return _dt_avg * 1e-6f; }

    void begin()
    {
        _start = clock::now();
    }

    void end()
    {
        auto dt = clock::now() - _start;
        _us_avg += ((float)std::chrono::duration_cast<std::chrono::microseconds>(dt).count() - _us_avg) / (float)_counter;
        ++_counter;
        _dt_avg = _us_avg;

        if (std::chrono::duration_cast<std::chrono::seconds>(clock::now() - _seconds).count() >= 1)
        {
            const std::string msg{ "Avg. frame (ms): " + std::to_string(_us_avg * 0.001f) + " " + std::to_string(_counter) + " fps\n" };
#if PRIMAL_PLATFORM_WINDOWS
            OutputDebugStringA(msg.c_str());
#else
            std::fputs(msg.c_str(), stderr);
#endif
            _us_avg = 0.f;
            _counter = 1;
            _seconds = clock::now();
        }
    }

private:
    float       _dt_avg{ 16.7f };
    float       _us_avg{ 0.f };
    int         _counter{ 1 };
    time_stamp  _start;
    time_stamp  _seconds{ clock::now() };
};
