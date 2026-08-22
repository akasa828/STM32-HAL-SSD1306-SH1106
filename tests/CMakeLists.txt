cmake_minimum_required(VERSION 3.20)

project(oled_host_tests LANGUAGES C CXX)

enable_testing()

configure_file(../Core/OLED/oled.cpp oled_under_test.cpp COPYONLY)

add_executable(oled_tests test_oled.cpp oled_under_test.cpp)

target_include_directories(oled_tests PRIVATE ../Core/OLED)
target_compile_features(oled_tests PRIVATE cxx_std_17)
target_compile_definitions(oled_tests PRIVATE
    OLED_WIDTH=16
    OLED_HEIGHT=16
    OLED_COLUMN_OFFSET=2
    OLED_ENABLE_WAVE=0
    OLED_USE_DOUBLE_BUFFER=1
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oled_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -ftrapv
    )
    target_link_options(oled_tests PRIVATE -ftrapv)
endif()

add_test(NAME oled_tests COMMAND oled_tests)

function(add_oled_mode_test target controller double_buffer offset)
    configure_file(../Core/OLED/oled.cpp ${target}_under_test.cpp COPYONLY)
    add_executable(${target} test_oled_modes.cpp ${target}_under_test.cpp)
    target_include_directories(${target} PRIVATE ../Core/OLED)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_definitions(${target} PRIVATE
        OLED_WIDTH=13
        OLED_HEIGHT=16
        OLED_CONTROLLER=${controller}
        OLED_COLUMN_OFFSET=${offset}
        OLED_ENABLE_WAVE=0
        OLED_USE_DOUBLE_BUFFER=${double_buffer}
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -ftrapv
        )
        target_link_options(${target} PRIVATE -ftrapv)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_oled_mode_test(oled_single_buffer_tests 0 0 0)
add_oled_mode_test(oled_sh1106_tests 1 1 2)

configure_file(../Core/OLED/oled.cpp oled_width255_under_test.cpp COPYONLY)
add_executable(oled_width255_tests
    test_oled_width255.cpp
    oled_width255_under_test.cpp
)
target_include_directories(oled_width255_tests PRIVATE ../Core/OLED)
target_compile_features(oled_width255_tests PRIVATE cxx_std_17)
target_compile_definitions(oled_width255_tests PRIVATE
    OLED_WIDTH=255
    OLED_HEIGHT=8
    OLED_ENABLE_WAVE=0
    OLED_USE_DOUBLE_BUFFER=0
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oled_width255_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -ftrapv
    )
    target_link_options(oled_width255_tests PRIVATE -ftrapv)
endif()
add_test(NAME oled_width255_tests COMMAND oled_width255_tests)

configure_file(../Core/OLED/oled.cpp oled_hal_under_test.cpp COPYONLY)
configure_file(../Core/Port/oled_stm32_hal.c oled_stm32_hal_under_test.c COPYONLY)
add_executable(oled_hal_adapter_tests
    test_oled_hal_adapter.cpp
    oled_hal_under_test.cpp
    oled_stm32_hal_under_test.c
)
target_include_directories(oled_hal_adapter_tests PRIVATE
    fakes
    ../Core/OLED
    ../Core/Port
)
target_compile_features(oled_hal_adapter_tests PRIVATE cxx_std_17)
target_compile_definitions(oled_hal_adapter_tests PRIVATE
    OLED_WIDTH=16
    OLED_HEIGHT=16
    OLED_ENABLE_WAVE=0
    OLED_USE_DOUBLE_BUFFER=0
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oled_hal_adapter_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -ftrapv
    )
    target_link_options(oled_hal_adapter_tests PRIVATE -ftrapv)
endif()
add_test(NAME oled_hal_adapter_tests COMMAND oled_hal_adapter_tests)
