cmake_minimum_required(VERSION 3.24)
project(Sentinel VERSION 1.0.7 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SENTINEL_BUILD_TESTS "Build Sentinel tests" ON)
option(SENTINEL_BUILD_TOOLS "Build Sentinel command-line tools" ON)
option(SENTINEL_BUILD_GUI "Build Sentinel native Windows GUI" ON)

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
    src/Simulation/RuleBasedTestModel.cpp
    src/Simulation/OpenAICompatibleModel.cpp
    src/Simulation/PersonaPolicy.cpp
    src/Simulation/SettingsStore.cpp
    src/Simulation/ResponseEvaluator.cpp
    src/Simulation/SessionStore.cpp
    src/Simulation/ConversationMemory.cpp
    src/Simulation/ModelRegistry.cpp
    src/Operations/Messaging.cpp
    src/Operations/Supervisor.cpp
    src/Agency/AgencyServer.cpp
    src/Update/UpdateService.cpp
)

target_include_directories(sentinel_core PUBLIC include)
target_link_libraries(sentinel_core PUBLIC unofficial::sqlite3::sqlite3)

if(WIN32)
    target_link_libraries(sentinel_core PUBLIC bcrypt crypt32 winhttp)
    target_compile_definitions(sentinel_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_executable(SentinelCli src/App/main.cpp)
target_link_libraries(SentinelCli PRIVATE sentinel_core)

if(WIN32 AND SENTINEL_BUILD_GUI)
    add_executable(Sentinel WIN32
        src/Gui/Main.cpp
        resources/SentinelVersion.rc
    )
    target_link_libraries(Sentinel PRIVATE
        sentinel_core
        d2d1
        dwrite
        dwmapi
        uxtheme
        windowscodecs
        comdlg32
        shell32
        shlwapi
        ole32
        uuid
        winhttp
        comctl32
    )
    target_compile_definitions(Sentinel PRIVATE UNICODE _UNICODE)
endif()

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

    add_executable(SentinelPlatformTests tests/Unit/PlatformTests.cpp)
    target_link_libraries(SentinelPlatformTests PRIVATE sentinel_core)
    add_test(NAME SentinelPlatformTests COMMAND SentinelPlatformTests)

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

if(WIN32 AND SENTINEL_BUILD_GUI)
    install(TARGETS Sentinel
        RUNTIME DESTINATION .
    )
endif()

if(SENTINEL_BUILD_TOOLS)
    install(TARGETS SentinelEvidenceVerifier
        RUNTIME DESTINATION bin
    )
endif()

install(DIRECTORY migrations/ DESTINATION migrations)
install(FILES README.md DESTINATION .)

install(FILES
    resources/SentinelConsole.cmd
    resources/Install-Sentinel.ps1
    resources/Uninstall-Sentinel.ps1
    DESTINATION .
)
