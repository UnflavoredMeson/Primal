// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "ToolsCommon.h"

namespace primal::tools
{
extern auto ShutDownTextureTools() -> void;
}

EDITOR_INTERFACE auto ShutDownContentTools() -> void
{
    using namespace primal::tools;
    ShutDownTextureTools();
}
