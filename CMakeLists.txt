cmake_minimum_required(VERSION 3.19)
project(binance_orderbook VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O3)
    endif()
elseif(MSVC)
    add_compile_options(/W4)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Od)
    else()
        add_compile_options(/O2)
    endif()
endif()

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Set CMake policy for Boost
cmake_policy(SET CMP0167 NEW)

# Find Boost with required components
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
# Note: system component is header-only in newer Boost versions
find_package(Boost 1.89 REQUIRED COMPONENTS headers thread)

# FetchContent for nlohmann_json if not found
include(FetchContent)

# Try to find nlohmann_json first
find_package(nlohmann_json QUIET)

if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, fetching from GitHub...")
    message(STATUS "If this fails, install nlohmann-json via: brew install nlohmann-json")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        CMAKE_ARGS -DCMAKE_POLICY_VERSION_MINIMUM=3.19
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Define the executable
add_executable(binance_orderbook main.cpp)

# Link libraries
target_link_libraries(binance_orderbook
    PRIVATE
        Boost::headers
        Boost::thread
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        nlohmann_json::nlohmann_json
)

# Include directories
target_include_directories(binance_orderbook
    PRIVATE
        ${Boost_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
)

# Platform-specific linking
if(WIN32)
    target_link_libraries(binance_orderbook PRIVATE ws2_32 wsock32)
endif()

# Set RPATH for Linux/Unix systems
if(UNIX AND NOT APPLE)
    set_target_properties(binance_orderbook PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(APPLE)
    set_target_properties(binance_orderbook PROPERTIES
        INSTALL_RPATH "@executable_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Installation rules
install(TARGETS binance_orderbook
    RUNTIME DESTINATION bin
)

# Optional: Create a script to build the project
if(UNIX)
    file(WRITE "${CMAKE_BINARY_DIR}/build.sh"
        "#!/bin/bash\n"
        "cmake -B build -S . -DCMAKE_BUILD_TYPE=Release\n"
        "cmake --build build --parallel $(nproc)\n"
    )
    file(CHMOD "${CMAKE_BINARY_DIR}/build.sh"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Boost version: ${Boost_VERSION}")
message(STATUS "  OpenSSL version: ${OPENSSL_VERSION}")
if(nlohmann_json_FOUND)
    message(STATUS "  nlohmann_json: Found (system)")
else()
    message(STATUS "  nlohmann_json: Using FetchContent")
endif()
message(STATUS "")
