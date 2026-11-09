// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#ifndef EDITOR_INTERFACE
#if defined(_MSC_VER)
#define EDITOR_INTERFACE extern "C" __declspec(dllexport)
#else
#define EDITOR_INTERFACE extern "C" __attribute__((visibility("default")))
#endif
#endif
