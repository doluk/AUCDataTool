#!/bin/bash
# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Builds, tests and packages the Linux installers (.deb, .rpm, .tar.gz with bundled Qt) on
# AlmaLinux 8 (glibc 2.28), so they run on RHEL/Rocky/Alma 8 and every newer distribution.
# Qt is built from source here (build-qt-el8.sh): Qt's own Linux binaries need glibc 2.34.
# gcc-toolset links the newer C++ runtime parts statically, so GCC 8's libstdc++ is enough.
#
# From the repository root (the volume keeps the Qt build, which takes an hour or more):
#   docker run --rm -v "$PWD":/src -v aucdatatool-qt:/opt/qt-el8 -w /src almalinux:8 \
#       bash packaging/linux/build-el8.sh
# Output: build-el8/package/. JOBS limits parallel compile jobs (memory).
# `build-el8.sh qt` only builds Qt (CI caches it separately).
set -euo pipefail

export QT_VERSION=${QT_VERSION:-6.12.1}
export QT_PREFIX=${QT_PREFIX:-/opt/qt-el8/$QT_VERSION}
BUILD=${BUILD:-build-el8}

dnf -y -q install epel-release dnf-plugins-core
dnf config-manager --set-enabled powertools
dnf -y -q install gcc-toolset-13-gcc-c++ gcc-toolset-13-binutils \
    python3.11 python3.11-pip rpm-build dpkg desktop-file-utils file findutils xz \
    mesa-libGL-devel mesa-libEGL-devel mesa-libgbm-devel libdrm-devel \
    libxkbcommon-devel libxkbcommon-x11-devel fontconfig-devel freetype-devel dbus-devel \
    glib2-devel cups-devel zlib-devel openssl3-devel at-spi2-core-devel \
    libX11-devel libxcb-devel xcb-util-devel xcb-util-cursor-devel xcb-util-wm-devel \
    xcb-util-image-devel xcb-util-keysyms-devel xcb-util-renderutil-devel libXrender-devel \
    wayland-devel libwayland-cursor libwayland-egl

# A current CMake and Ninja (EL8 ships CMake 3.26, Ninja only in PowerTools).
python3.11 -m pip install -q cmake ninja
export PATH=/usr/local/bin:$PATH
cmake --version | head -n1

bash "$(dirname "$0")/build-qt-el8.sh"
[ "${1:-}" = qt ] && exit 0
QT=$QT_PREFIX

export CC=/opt/rh/gcc-toolset-13/root/usr/bin/gcc CXX=/opt/rh/gcc-toolset-13/root/usr/bin/g++
# lib, not EL's lib64: Qt's plugins find their libraries at ../../lib as shipped. With
# another directory Qt rewrites their runtime paths, outside the CPack staging tree.
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT" \
    -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$BUILD" --parallel "${JOBS:-$(nproc)}"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD" --output-on-failure

desktop-file-validate app/linux/io.github.doluk.AUCDataTool.desktop
cpack --config "$BUILD/CPackConfig.cmake" -C Release -B "$BUILD/package"

# Every ELF file shipped must run with glibc 2.28 and GCC 8's libstdc++ (GLIBCXX 3.4.25).
check=$(mktemp -d)
tar -xzf "$BUILD"/package/*.tar.gz -C "$check"
newest() { grep -ho "$1[0-9.]*" "$2" | sed "s/^$1//" | sort -uV | tail -n1; }
symbols=$(mktemp)
find "$check" -type f -print0 | while IFS= read -r -d '' f; do
    if file -b "$f" | grep -q '^ELF'; then objdump -T "$f" >> "$symbols" 2>/dev/null || true; fi
done
glibc=$(newest GLIBC_ "$symbols")
glibcxx=$(newest GLIBCXX_ "$symbols")
echo "newest symbol versions: GLIBC_$glibc GLIBCXX_$glibcxx"
[ "$(printf '%s\n2.28\n' "$glibc" | sort -V | tail -n1)" = "2.28" ] || { echo "needs glibc > 2.28"; exit 1; }
[ "$(printf '%s\n3.4.25\n' "$glibcxx" | sort -V | tail -n1)" = "3.4.25" ] || { echo "needs GLIBCXX > 3.4.25"; exit 1; }

# Both platform plugins: Wayland (with the xdg-shell integration and EGL buffers) and X11;
# TLS for the update check.
for p in platforms/libqxcb.so platforms/libqwayland.so \
         wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
         wayland-shell-integration/libxdg-shell.so tls/libqopensslbackend.so; do
    find "$check" -path "*/plugins/$p" | grep . > /dev/null || { echo "missing plugin $p"; exit 1; }
done

# Desktop integration in the packages.
dpkg-deb -c "$BUILD"/package/*.deb | grep 'share/applications/io.github.doluk.AUCDataTool.desktop' > /dev/null
dpkg-deb -c "$BUILD"/package/*.deb | grep 'share/icons/hicolor/scalable/apps/io.github.doluk.AUCDataTool.svg' > /dev/null
dpkg-deb -c "$BUILD"/package/*.deb | grep 'share/mime/packages/io.github.doluk.AUCDataTool.xml' > /dev/null
dpkg-deb -I "$BUILD"/package/*.deb postinst | grep 'ln -sf' > /dev/null
rpm -qp --scripts "$BUILD"/package/*.rpm | grep 'ln -sf' > /dev/null
ls -l "$BUILD"/package/
