cmake_minimum_required(VERSION 3.20)
project(usb_daq_live_node LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(SDL2 CONFIG REQUIRED)

set(USB_DAQ_PLATFORM_SOURCES)
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    find_package(Threads REQUIRED)
    list(APPEND USB_DAQ_PLATFORM_SOURCES
        src/usb_daq_libusb_backend.cpp
        src/usb_daq_protocol.cpp)
endif()

include(FetchContent)

FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_Declare(implot
    URL https://github.com/epezent/implot/archive/refs/tags/v0.16.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(imgui implot json)

add_executable(usb_daq_live_node
    src/main.cpp
    src/acquisition.cpp
    ${USB_DAQ_PLATFORM_SOURCES}
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp)

target_include_directories(usb_daq_live_node PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR})

target_link_libraries(usb_daq_live_node PRIVATE
    OpenGL::GL
    SDL2::SDL2
    nlohmann_json::nlohmann_json)

if(NOT WIN32)
    target_link_libraries(usb_daq_live_node PRIVATE
        PkgConfig::LIBUSB
        Threads::Threads)
endif()

target_compile_definitions(usb_daq_live_node PRIVATE SDL_MAIN_HANDLED)

if(WIN32)
    target_compile_definitions(usb_daq_live_node PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
    target_compile_options(usb_daq_live_node PRIVATE
        /W4 /utf-8 /EHsc
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/O2>)
else()
    target_compile_options(usb_daq_live_node PRIVATE -O2 -Wall -Wextra)
endif()

set(VENDOR_DLL "${CMAKE_CURRENT_SOURCE_DIR}/../Usb_Daq_V52_Dll.dll")
if(WIN32 AND EXISTS "${VENDOR_DLL}")
    add_custom_command(TARGET usb_daq_live_node POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VENDOR_DLL}" "$<TARGET_FILE_DIR:usb_daq_live_node>/Usb_Daq_V52_Dll.dll")
endif()

include(CTest)
if(BUILD_TESTING AND NOT WIN32)
    add_executable(usb_daq_protocol_tests
        tests/protocol_tests.cpp
        src/usb_daq_protocol.cpp)
    target_include_directories(usb_daq_protocol_tests PRIVATE src)
    target_compile_options(usb_daq_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME usb_daq_protocol_tests COMMAND usb_daq_protocol_tests)
endif()

add_custom_command(TARGET usb_daq_live_node POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/daq_config.jsonc"
            "$<TARGET_FILE_DIR:usb_daq_live_node>/daq_config.jsonc")
