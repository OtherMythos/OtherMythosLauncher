cmake_minimum_required(VERSION 3.22)

#Static CRT, so the exe needs no Visual C++ redistributable. Set before project() and without
#an if(MSVC), which isn't defined until project() has run; other compilers ignore it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(OtherMythosLauncher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(OML_BUILD_TESTS "Build the tests" ON)

find_package(Threads REQUIRED)
include(cmake/Dependencies.cmake)

find_package(Git QUIET)
set(OML_VERSION "dev")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE OML_GIT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(OML_GIT_VERSION)
        set(OML_VERSION ${OML_GIT_VERSION})
    endif()
endif()
configure_file(src/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h)
include(cmake/EmbedFile.cmake)
#The OtherMythos cat, from othermythos.com's icon.jpg.
embed_file(${CMAKE_CURRENT_SOURCE_DIR}/res/cat.png ${CMAKE_CURRENT_BINARY_DIR}/generated/CatImage.h kCatPng)

#Everything that doesn't need a window lives in launcherCore, so the tests can drive it
#without a display.
add_library(launcherCore STATIC
    src/Archive.cpp
    src/BuildIndex.cpp
    src/Catalog.cpp
    src/Config.cpp
    src/Http.cpp
    src/Installer.cpp
    src/Json.cpp
    src/Paths.cpp
    src/Runner.cpp
    src/SafePath.cpp
    src/Sha256.cpp
    src/TaskQueue.cpp
    src/TimeFormat.cpp
)
if(WIN32)
    target_sources(launcherCore PRIVATE src/HttpWinHttp.cpp src/ChildProcessWin.cpp)
    target_link_libraries(launcherCore PUBLIC winhttp)
    #_CRT_SECURE_NO_WARNINGS: MSVC otherwise asks for its own _s variants of standard C calls.
    target_compile_definitions(launcherCore PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    target_sources(launcherCore PRIVATE src/HttpCurl.cpp src/ChildProcessPosix.cpp)
endif()
target_include_directories(launcherCore PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(launcherCore PUBLIC cjson miniz Threads::Threads ${CMAKE_DL_LIBS})

add_executable(OtherMythosLauncher WIN32
    src/main.cpp
    src/App.cpp
    src/Capture.cpp
    src/GamePanel.cpp
    src/Theme.cpp
    src/Ui.cpp
    src/Widgets.cpp
)
if(WIN32)
    #Merged into the linker's manifest: long paths, UTF-8 for narrow APIs, per-monitor DPI.
    target_sources(OtherMythosLauncher PRIVATE res/launcher.manifest)
endif()
target_link_libraries(OtherMythosLauncher PRIVATE launcherCore imgui SDL3::SDL3-static)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    #Only libc and friends stay dynamic, so one binary runs across distros and on SteamOS.
    target_link_options(OtherMythosLauncher PRIVATE -static-libstdc++ -static-libgcc -Wl,--gc-sections)
endif()
if(NOT MSVC)
    #Lets the linker drop the parts of SDL and ImGui the launcher never calls.
    foreach(target launcherCore OtherMythosLauncher imgui cjson miniz SDL3-static)
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    endforeach()
endif()

if(MSVC)
    target_compile_options(launcherCore PRIVATE /W4 /utf-8)
    target_compile_options(OtherMythosLauncher PRIVATE /W4 /utf-8)
else()
    target_compile_options(launcherCore PRIVATE -Wall -Wextra)
    target_compile_options(OtherMythosLauncher PRIVATE -Wall -Wextra)
endif()

if(OML_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
