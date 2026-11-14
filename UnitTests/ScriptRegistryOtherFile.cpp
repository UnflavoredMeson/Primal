// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#include "ScriptRegistryTests.h"

// The second file that includes ScriptRegistryTests.h (see there).
namespace script_registry_test
{

auto registration_seen_by_other_file() -> const u8 *
{
    return &primal_script_registration_HeaderScript;
}

}
