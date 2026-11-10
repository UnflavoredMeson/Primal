// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once

// Convenience umbrella: only includes. None of the headers below include this file back.
#include "CommonDefinitions.h"
#include "PlatformDetection.h"

// C/C++ headers
// NOTE: don't put here any headers that include std::vector or std::deque
#include <assert.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

// common headers
#include "../Common/Id.h"
#include "../Utilities/FreeList.h"
#include "../Utilities/Math.h"
#include "../Utilities/Utilities.h"
#include "PrimitiveTypes.h"
