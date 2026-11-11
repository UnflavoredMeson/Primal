# Copyright (c) 2026 Charles Smith
# Distributed under the MIT license. See the LICENSE file in the project root for more information.
#
# Build image for the Linux CI jobs (.gitea/workflows/linux.yml). Building and publishing it: README.md, next to
# this file.
#
# Arch Linux, with every package taken from one dated snapshot of the Arch Linux Archive. The toolchain therefore
# only changes when ARCH_SNAPSHOT is changed, not whenever the image happens to be rebuilt. That matters because the
# build uses -Werror and clang-tidy must stay at 0 warnings: a new compiler can add warnings.
FROM archlinux:base

# Snapshot date (YYYY/MM/DD) of https://archive.archlinux.org/repos/.
ARG ARCH_SNAPSHOT=2026/11/09

# -Syyuu: also downgrade what the base image has that is newer than the snapshot.
RUN echo "Server = https://archive.archlinux.org/repos/${ARCH_SNAPSHOT}/\$repo/os/\$arch" > /etc/pacman.d/mirrorlist \
    && pacman -Syyuu --noconfirm --needed \
        # the build: clang (with clang-tidy, clang-format, run-clang-tidy), compiler-rt for ASan/UBSan
        clang compiler-rt gcc cmake ninja \
        # engine dependencies (cmake/PrimalDependencies.cmake, Engine/CMakeLists.txt)
        sdl3 vulkan-headers vulkan-icd-loader directx-shader-compiler \
        # Tools/*.py, Tools/CI/ci.sh
        python git \
        # the graphics tests (GraphicsTests/): a software Vulkan device (lavapipe), the validation layer, and a
        # virtual X display (xvfb-run)
        vulkan-swrast vulkan-validation-layers xorg-server-xvfb \
        # the Actions runner runs JavaScript actions (actions/checkout) with the container's node
        nodejs \
    && pacman -Scc --noconfirm \
    && rm -rf /var/cache/pacman/pkg/* /var/lib/pacman/sync/*
