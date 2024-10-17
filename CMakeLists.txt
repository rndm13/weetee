cmake_minimum_required(VERSION 3.16)
project(weetee)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake")
include(cotire)
include(ExternalProject)

set(CMAKE_GENERATOR "Ninja")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (!WIN32)
    set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wshadow -Wnon-virtual-dtor -pedantic -Wold-style-cast -Wcast-align -Wunused -Wconversion -Wuseless-cast -Wnull-dereference -Wlogical-op $<$<CONFIG:DEBUG>:-fsanitize=address,undefined,leak>")
else ()
    set(CMAKE_CXX_FLAGS "")
endif ()

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_MultipleHeaders OFF CACHE INTERNAL "")

set(BS_thread_pool_SOURCE_DIR external/thread-pool/)

# Add imgui_bundle
# =================

if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui_bundle)
    add_subdirectory(external/imgui_bundle)
endif()

# Build app
# ==============

imgui_bundle_add_app(weetee src/main.cpp)
target_link_libraries(weetee PRIVATE
    "$<$<CONFIG:DEBUG>:-fsanitize=address,undefined,leak>"
)

# Add HTTPS
# ==================

set(OPENSSL_USE_STATIC_LIBS TRUE)

find_package(httplib COMPONENTS OpenSSL ZLIB Brotli)
if (NOT httplib_FOUND)
    add_subdirectory(external/cpp-httplib)
endif()

if (HTTPLIB_IS_USING_OPENSSL)
    set_target_properties(weetee PROPERTIES COMPILE_DEFINITIONS "CPPHTTPLIB_OPENSSL_SUPPORT=1")
endif()
if (HTTPLIB_IS_USING_ZLIB)
    set_target_properties(weetee PROPERTIES COMPILE_DEFINITIONS "CPPHTTPLIB_ZLIB_SUPPORT=1")
endif()
if (HTTPLIB_IS_USING_BROTLI)
    set_target_properties(weetee PROPERTIES COMPILE_DEFINITIONS "CPPHTTPLIB_BROTLI_SUPPORT=1")
endif()

message("HTTPLIB_IS_USING_OPENSSL = ${HTTPLIB_IS_USING_OPENSSL}")
message("HTTPLIB_IS_USING_ZLIB = ${HTTPLIB_IS_USING_ZLIB}")
message("HTTPLIB_IS_USING_BROTLI = ${HTTPLIB_IS_USING_BROTLI}")

# Add json 
# =============== 

if (NOT TARGET nlohmann_json) 
    add_subdirectory(external/json)
    add_library(nljson ALIAS nlohmann_json::nlohmann_json)
else()
    add_library(nljson ALIAS nlohmann_json)
endif()

# Add Thread Pool
# ===============

add_library(BS_thread_pool INTERFACE)
target_include_directories(BS_thread_pool INTERFACE ${BS_thread_pool_SOURCE_DIR}/include)

# Add GTest
# ===============

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Add tests
# ===============

enable_testing()

include(GoogleTest)

add_subdirectory(tests)

# Add src
# ===============

add_subdirectory(src)

target_link_libraries(weetee PRIVATE 
    utils textinputcombo
    app_state save_state tests
    json http
    gui gui_tests)

if(WIN32)
  target_link_libraries(weetee PRIVATE wsock32 ws2_32)
endif()

cotire(weetee)
