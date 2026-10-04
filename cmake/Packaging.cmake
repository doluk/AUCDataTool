# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

# Installers via CPack: NSIS (.exe) on Windows, DragNDrop (.dmg) on macOS, .deb/.rpm/.tar.gz
# on Linux. The Arch Linux package is built natively from packaging/arch/PKGBUILD.
#   cmake --build build --config Release
#   cpack --config build/CPackConfig.cmake -C Release -B build/package

set(CPACK_PACKAGE_NAME "AUCDataTool")
set(CPACK_PACKAGE_VENDOR "Lukas Dobler")
set(CPACK_PACKAGE_CONTACT "Lukas Dobler")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/doluk/AUCDataTool")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "AUCDataTool")
set(CPACK_PACKAGE_EXECUTABLES "AUCDataTool" "AUCDataTool")

# CPack generators expect a license file with an extension.
configure_file("${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_BINARY_DIR}/LICENSE.txt" COPYONLY)
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_BINARY_DIR}/LICENSE.txt")

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "AUCDataTool")
    set(CPACK_NSIS_DISPLAY_NAME "AUCDataTool ${PROJECT_VERSION}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\AUCDataTool.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "AUCDataTool")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    # DEB/RPM install to /opt (see CPackOptions.cmake) so the bundled Qt does not collide
    # with system Qt; the tarball is relocatable.
    set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/CPackOptions.cmake")

    # Runtime requirements of the bundled Qt xcb platform plugin.
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libgl1, libegl1, libfontconfig1, libfreetype6, libdbus-1-3, libxkbcommon0, \
libxkbcommon-x11-0, libx11-xcb1, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, \
libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-shape0, libxcb-sync1, \
libxcb-xfixes0, libxcb-xinerama0, libxcb-xkb1")

    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_LICENSE "LGPL-3.0-or-later")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Science")
    # Automatic dependencies would require/provide the bundled Qt libraries.
    set(CPACK_RPM_PACKAGE_AUTOREQPROV OFF)
    set(CPACK_RPM_PACKAGE_REQUIRES
        "libglvnd-opengl, libglvnd-egl, fontconfig, freetype, dbus-libs, libxkbcommon, \
libxkbcommon-x11, libX11-xcb, libxcb, xcb-util-cursor, xcb-util-wm, xcb-util-image, \
xcb-util-keysyms, xcb-util-renderutil")
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /opt)
    set(CPACK_RPM_SPEC_MORE_DEFINE "%define debug_package %{nil}")
endif()

include(CPack)
