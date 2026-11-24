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
auc_add_test(tst_xl)
auc_add_test(tst_scantiming)

# UpdateInfo is header-only (Qt Core) so it also runs with -DAUC_BUILD_APP=OFF.
auc_add_test(tst_updateinfo)
target_include_directories(tst_updateinfo PRIVATE ${PROJECT_SOURCE_DIR}/app)

if(AUC_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Quick)
    add_executable(tst_plotstyle
        tst_plotstyle.cpp
        ${PROJECT_SOURCE_DIR}/app/ScanPlot.h
        ${PROJECT_SOURCE_DIR}/app/ScanPlot.cpp
        ${PROJECT_SOURCE_DIR}/app/PlotAxis.h
    )
    target_include_directories(tst_plotstyle PRIVATE ${PROJECT_SOURCE_DIR}/app)
    target_link_libraries(tst_plotstyle PRIVATE Qt6::Quick Qt6::Test)
    add_test(NAME tst_plotstyle COMMAND tst_plotstyle)
    set_tests_properties(tst_plotstyle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
