add_executable(aucinfo aucinfo.cpp)
target_link_libraries(aucinfo PRIVATE Auc::Core)

add_executable(aucgen aucgen.cpp)
target_link_libraries(aucgen PRIVATE Auc::Core)
