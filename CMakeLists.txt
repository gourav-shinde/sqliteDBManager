cmake_minimum_required(VERSION 3.14)
project(sqlite3db
    VERSION 1.0.0
    DESCRIPTION "SQLite3 Database Library with Industry Best Practices"
    LANGUAGES CXX
)

# ============================================================
# Build Configuration
# ============================================================

# Require C++17 (for std::optional, std::variant, structured bindings)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler warnings (INDUSTRY PRACTICE: Enable warnings)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)
endif()

# ============================================================
# Dependencies
# ============================================================

# Find SQLite3
find_package(SQLite3 REQUIRED)

# ============================================================
# Library Target
# ============================================================

# Create the library
add_library(sqlite3db
    src/connection.cpp
    src/statement.cpp
    src/transaction.cpp
    src/migration.cpp
    src/repository.cpp
)

# Include directories
target_include_directories(sqlite3db
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link SQLite3
target_link_libraries(sqlite3db
    PUBLIC
        SQLite::SQLite3
)

# ============================================================
# Examples
# ============================================================

option(BUILD_EXAMPLES "Build example programs" ON)

if(BUILD_EXAMPLES)
    add_executable(sqlite3db_example
        examples/main.cpp
    )

    target_link_libraries(sqlite3db_example
        PRIVATE
            sqlite3db
    )
endif()

# ============================================================
# Testing (Optional)
# ============================================================

option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()

    # Add test executable
    add_executable(sqlite3db_tests
        tests/test_main.cpp
    )

    target_link_libraries(sqlite3db_tests
        PRIVATE
            sqlite3db
    )

    add_test(NAME sqlite3db_tests COMMAND sqlite3db_tests)
endif()

# ============================================================
# Installation (INDUSTRY PRACTICE: Proper install rules)
# ============================================================

include(GNUInstallDirs)

# Install library
install(TARGETS sqlite3db
    EXPORT sqlite3dbTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY include/sqlite3db
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install CMake config files (for find_package support)
install(EXPORT sqlite3dbTargets
    FILE sqlite3dbTargets.cmake
    NAMESPACE sqlite3db::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sqlite3db
)

# Print configuration summary
message(STATUS "")
message(STATUS "sqlite3db ${PROJECT_VERSION} configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build examples: ${BUILD_EXAMPLES}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "")
