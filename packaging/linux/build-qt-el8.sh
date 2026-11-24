#!/bin/bash
# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Builds the Qt modules AUCDataTool uses from source on AlmaLinux 8, for the Linux packages:
# Qt's own Linux binaries need glibc 2.34 since Qt 6.10, RHEL 8 has 2.28. Built with
# gcc-toolset-13, which links the newer C++ runtime parts statically, so the result needs only
# glibc 2.28 and GCC 8's libstdc++, like the app. Called by build-el8.sh (which installs the
# build dependencies first); skipped if $QT_PREFIX already holds a complete build (CI cache).
#
#   QT_VERSION  Qt release to build (default below)
#   QT_PREFIX   install prefix (default /opt/qt-el8/$QT_VERSION)
#   JOBS        parallel compile jobs (default: all cores)
set -euo pipefail

QT_VERSION=${QT_VERSION:-6.12.1}
QT_PREFIX=${QT_PREFIX:-/opt/qt-el8/$QT_VERSION}
JOBS=${JOBS:-$(nproc)}
# Dependency order. qtsvg: SVG icons; qtwayland: native Wayland; qtquick3d + qtshadertools:
# Qt Graphs' 3D surface view.
MODULES="qtbase qtshadertools qtsvg qtdeclarative qtwayland qtquick3d qtgraphs"

if [ -f "$QT_PREFIX/.complete" ]; then
    echo "Qt $QT_VERSION already built in $QT_PREFIX"
    exit 0
fi

export CC=/opt/rh/gcc-toolset-13/root/usr/bin/gcc CXX=/opt/rh/gcc-toolset-13/root/usr/bin/g++
series=${QT_VERSION%.*}
url=https://download.qt.io/official_releases/qt/$series/$QT_VERSION/submodules
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl -fsSL "$url/md5sums.txt" -o "$work/md5sums.txt"
for m in $MODULES; do
    # Per-module stamps: an interrupted build (e.g. out of memory) resumes where it stopped.
    [ -f "$QT_PREFIX/.built-$m" ] && { echo "== $m (already built)"; continue; }
    tarball=$m-everywhere-src-$QT_VERSION.tar.xz
    echo "== $m"
    curl -fsSL "$url/$tarball" -o "$work/$tarball"
    (cd "$work" && grep " $tarball\$" md5sums.txt | md5sum -c --quiet -)
    tar -xJf "$work/$tarball" -C "$work"
    rm "$work/$tarball"

    extra=()
    if [ "$m" = qtbase ]; then
        # Bundled third-party libraries rather than the build system's, so the packages
        # depend only on what every distribution has. OpenSSL 3 (EPEL's openssl3-devel) is
        # loaded at run time: HTTPS for the update check where libssl.so.3 is installed.
        extra=(-DFEATURE_xcb=ON -DFEATURE_xcb_xlib=ON -DFEATURE_xkbcommon_x11=ON -DFEATURE_dbus=ON
               -DFEATURE_accessibility_atspi_bridge=ON
               # Not in Qt's own Linux binaries either; they would add libSM/libICE and
               # libgssapi_krb5 to the system libraries the packages need.
               -DFEATURE_xcb_sm=OFF -DFEATURE_gssapi=OFF
               -DFEATURE_fontconfig=ON -DFEATURE_system_freetype=ON -DFEATURE_cups=ON
               -DFEATURE_icu=OFF -DFEATURE_system_harfbuzz=OFF -DFEATURE_system_pcre2=OFF
               -DFEATURE_system_png=OFF -DFEATURE_system_jpeg=OFF -DFEATURE_system_doubleconversion=OFF
               -DFEATURE_system_libb2=OFF -DFEATURE_system_zlib=ON -DFEATURE_sql=OFF
               -DFEATURE_openssl=ON -DFEATURE_openssl_linked=OFF
               -DOPENSSL_INCLUDE_DIR=/usr/include/openssl3
               -DOPENSSL_SSL_LIBRARY=/usr/lib64/openssl3/libssl.so
               -DOPENSSL_CRYPTO_LIBRARY=/usr/lib64/openssl3/libcrypto.so)
    elif [ "$m" = qtwayland ]; then
        extra=(-DFEATURE_wayland_client=ON -DFEATURE_wayland_egl=ON)
    fi
    cmake -S "$work/$m-everywhere-src-$QT_VERSION" -B "$work/build-$m" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$QT_PREFIX" -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DQT_GENERATE_SBOM=OFF "${extra[@]}" > "$work/configure-$m.log" \
        || { tail -n 80 "$work/configure-$m.log"; exit 1; }
    cmake --build "$work/build-$m" --parallel "$JOBS"
    cmake --install "$work/build-$m" > /dev/null
    rm -rf "$work/build-$m" "$work/$m-everywhere-src-$QT_VERSION"
    touch "$QT_PREFIX/.built-$m"
done
touch "$QT_PREFIX/.complete"
echo "Qt $QT_VERSION installed in $QT_PREFIX"
