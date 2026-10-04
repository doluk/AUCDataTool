# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

function(auc_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Auc::Core Qt6::Test)
    # Tests may use internal helpers such as the CRC routine.
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/core/src)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

auc_add_test(tst_aucfile)
auc_add_test(tst_processing)
auc_add_test(tst_noise)
auc_add_test(tst_folderwatcher)
auc_add_test(tst_mwl)
