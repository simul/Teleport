cmake_minimum_required(VERSION 3.8)
file( GLOB SRC "*.cpp" )
add_executable(test_client ${SRC} )
target_compile_features(test_client PRIVATE cxx_std_17)
set_target_properties( test_client PROPERTIES FOLDER AVStream/Test)
target_link_libraries(test_client testapi renderer libavstream cxxopts)
target_include_directories(test_client PUBLIC ../thirdparty/enet/Include )