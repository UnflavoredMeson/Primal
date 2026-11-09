// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "Test.h"

class EngineTest : public Test
{
  public:
    auto initialize() -> bool override;
    auto run() -> void override;
    auto shutdown() -> void override;
};
