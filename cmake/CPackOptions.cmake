# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

# Per-generator settings, evaluated by cpack for each generator.
if(CPACK_GENERATOR MATCHES "^(DEB|RPM)$")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/AUCDataTool")
elseif(CPACK_GENERATOR STREQUAL "TGZ" AND CPACK_AUC_TARBALL_EXTRAS)
    # The tarball has no package manager: an install script and instructions at its top.
    set(CPACK_INSTALLED_DIRECTORIES "${CPACK_AUC_TARBALL_EXTRAS};.")
endif()
