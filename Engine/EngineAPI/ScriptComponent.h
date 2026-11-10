// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "../Common/CommonHeaders.h"

namespace primal::script
{

DEFINE_TYPED_ID(ScriptId);

class Component final
{
  public:
    constexpr explicit Component(ScriptId id)
        : _id{id}
    {
    }
    constexpr Component()
        : _id{id::invalid_id}
    {
    }
    constexpr auto get_id() const -> ScriptId
    {
        return _id;
    }
    constexpr auto is_valid() const -> bool
    {
        return id::is_valid(_id);
    }

  private:
    ScriptId _id;
};

}
