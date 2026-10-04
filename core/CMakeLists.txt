# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

add_library(auccore STATIC
    include/auc/Dataset.h
    include/auc/AucFile.h
    include/auc/Processing.h
    include/auc/Noise.h
    include/auc/FolderWatcher.h
    src/Crc32.h
    src/Crc32.cpp
    src/Dataset.cpp
    src/AucFile.cpp
    src/Processing.cpp
    src/Noise.cpp
    src/FolderWatcher.cpp
)
add_library(Auc::Core ALIAS auccore)

target_include_directories(auccore PUBLIC include)
target_link_libraries(auccore PUBLIC Qt6::Core)
set_target_properties(auccore PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
