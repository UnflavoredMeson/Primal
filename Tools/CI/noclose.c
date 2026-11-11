// Copyright (c) 2026 Charles Smith
// Distributed under the MIT license. See the LICENSE file in the project root for more information.

// LD_PRELOAD shim for the sanitizer run of the graphics tests (ci.sh): dlclose() does nothing.
// The Vulkan loader unloads the driver and the layers when the instance is destroyed. LeakSanitizer reports at
// exit, and by then it can't name the frames of what they left allocated ("<unknown module>"), so those leaks
// can't be told apart from the engine's or suppressed. With the libraries still loaded they have names, and
// lsan.supp lists the ones that aren't ours.
int dlclose(void *handle)
{
    (void)handle;
    return 0;
}
