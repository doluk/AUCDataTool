# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

add_executable(aucinfo aucinfo.cpp)
target_link_libraries(aucinfo PRIVATE Auc::Core)

add_executable(aucgen aucgen.cpp)
target_link_libraries(aucgen PRIVATE Auc::Core)

add_executable(mwlgen mwlgen.cpp)
target_link_libraries(mwlgen PRIVATE Auc::Core)
