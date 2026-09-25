cmake_minimum_required(VERSION 3.24)
project(Sentinel VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SENTINEL_BUILD_TESTS "Build Sentinel tests" ON)
option(SENTINEL_BUILD_TOOLS "Build Sentinel command-line tools" ON)

find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(sentinel_core
    src/Core/Uuid.cpp
    src/Security/SecureBuffer.cpp
    src/Security/WindowsCrypto.cpp
    src/Security/WindowsDpapiSecretProtector.cpp
    src/Security/KeyManager.cpp
    src/Storage/SqliteDatabase.cpp
    src/Storage/MigrationService.cpp
    src/Core/CaseRepository.cpp
    src/Core/CaseService.cpp
    src/Audit/AuditService.cpp
    src/Evidence/SevContainer.cpp
    src/Evidence/EvidenceService.cpp
)

target_include_directories(sentinel_core PUBLIC include)
target_link_libraries(sentinel_core PUBLIC unofficial::sqlite3::sqlite3)

if(WIN32)
    target_link_libraries(sentinel_core PUBLIC bcrypt crypt32)
    target_compile_definitions(sentinel_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_executable(SentinelCli src/App/main.cpp)
target_link_libraries(SentinelCli PRIVATE sentinel_core)

if(SENTINEL_BUILD_TOOLS)
    add_executable(SentinelEvidenceVerifier tools/SentinelEvidenceVerifier/main.cpp)
    target_link_libraries(SentinelEvidenceVerifier PRIVATE sentinel_core)
endif()

if(SENTINEL_BUILD_TESTS)
    enable_testing()
    add_executable(SentinelCoreTests tests/Unit/CoreTests.cpp)
    target_link_libraries(SentinelCoreTests PRIVATE sentinel_core)
    target_compile_definitions(SentinelCoreTests PRIVATE SENTINEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME SentinelCoreTests COMMAND SentinelCoreTests)
    add_test(
        NAME SentinelCliSelfTest
        COMMAND SentinelCli "${CMAKE_BINARY_DIR}/sentinel-self-test-data" self-test
    )
    set_tests_properties(
        SentinelCliSelfTest
        PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

install(TARGETS SentinelCli
    RUNTIME DESTINATION bin
)

if(SENTINEL_BUILD_TOOLS)
    install(TARGETS SentinelEvidenceVerifier
        RUNTIME DESTINATION bin
    )
endif()

install(DIRECTORY migrations/ DESTINATION migrations)
install(FILES README.md DESTINATION .)

install(FILES resources/SentinelConsole.cmd DESTINATION .)
