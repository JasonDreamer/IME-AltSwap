cmake_minimum_required(VERSION 3.24)

project(IMEAltSwap VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(IME-AltSwap WIN32
    src/App.cpp
    src/App.h
    src/AppMessages.h
    src/AltTapDetector.cpp
    src/AltTapDetector.h
    src/InputHookHost.cpp
    src/InputHookHost.h
    src/ImeController.cpp
    src/ImeController.h
    src/ImeSwitchCoordinator.cpp
    src/ImeSwitchCoordinator.h
    src/StartupManager.cpp
    src/StartupManager.h
    src/main.cpp
    src/app.rc
    src/app.manifest
    src/resource.h
)

target_compile_definitions(IME-AltSwap PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(IME-AltSwap PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
)

set_property(TARGET IME-AltSwap PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(IME-AltSwap PRIVATE
    Advapi32
    Imm32
    Shell32
    User32
)

include(CTest)
if(BUILD_TESTING)
    add_executable(AltTapDetectorTests
        tests/AltTapDetectorTests.cpp
        src/AltTapDetector.cpp
    )
    target_include_directories(AltTapDetectorTests PRIVATE src)
    target_compile_definitions(AltTapDetectorTests PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(AltTapDetectorTests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
    )
    set_property(TARGET AltTapDetectorTests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_test(NAME AltTapDetectorTests COMMAND AltTapDetectorTests)

    add_executable(ImeRuntimeProbe WIN32 tests/ImeRuntimeProbe.cpp)
    target_compile_definitions(ImeRuntimeProbe PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(ImeRuntimeProbe PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
    )
    set_property(TARGET ImeRuntimeProbe PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(ImeRuntimeProbe PRIVATE Imm32 User32)

    add_executable(ImePhysicalProbe WIN32 tests/ImePhysicalProbe.cpp)
    target_compile_definitions(ImePhysicalProbe PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(ImePhysicalProbe PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
    )
    set_property(TARGET ImePhysicalProbe PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(ImePhysicalProbe PRIVATE Imm32 User32)

    add_executable(ImeForegroundProbe WIN32 tests/ImeForegroundProbe.cpp)
    target_compile_definitions(ImeForegroundProbe PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(ImeForegroundProbe PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
    )
    set_property(TARGET ImeForegroundProbe PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(ImeForegroundProbe PRIVATE Imm32 User32)
endif()
