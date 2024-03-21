cmake_minimum_required(VERSION 3.12)
project(weetee)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add imgui_bundle
# =================
# 1/  Option 1: if you added imgui_bundle in a subfolder, you can add it to your project with:
if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui_bundle)
    add_subdirectory(external/imgui_bundle)
endif()

## 2/  Option 2: simply fetch imgui_bundle during the build
if (NOT TARGET imgui_bundle)
    message(STATUS "Fetching imgui_bundle...")
    include(FetchContent)
    Set(FETCHCONTENT_QUIET FALSE)
    FetchContent_Declare(imgui_bundle GIT_REPOSITORY https://github.com/pthom/imgui_bundle.git GIT_TAG main)
    FetchContent_MakeAvailable(imgui_bundle)

    # Uncomment the next line if you which to also automatically fetch and compile OpenCV for immvision support
    # set(IMMVISION_FETCH_OPENCV ON)
endif()

# Build app
# ==============

imgui_bundle_add_app(weetee src/main.cpp)

# Add https and json
# ==================

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL)
if (${OpenSSL_FOUND})
    target_link_libraries(
        weetee PRIVATE
        OpenSSL::Crypto
        OpenSSL::SSL)
    set_target_properties(weetee PROPERTIES COMPILE_DEFINITIONS "OPENSSL=1")
else (${OpenSSL_FOUND})
    set_target_properties(weetee PROPERTIES COMPILE_DEFINITIONS "OPENSSL=0")
endif (${OpenSSL_FOUND})

add_subdirectory(external/cpp-httplib)
add_subdirectory(external/json)

target_link_libraries(
    weetee PRIVATE
    httplib::httplib
    nlohmann_json::nlohmann_json)

# Add Thread Pool
# ===============

set(BS_thread_pool_SOURCE_DIR external/thread-pool/)
add_library(BS_thread_pool INTERFACE)
target_include_directories(BS_thread_pool INTERFACE ${BS_thread_pool_SOURCE_DIR}/include)

target_link_libraries(
    weetee PRIVATE
    BS_thread_pool)
