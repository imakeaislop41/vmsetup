cmake_minimum_required(VERSION 3.15)
project(VMSetup LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded") # /MT - static CRT

add_executable(VMSetup VMSetup.cpp)

target_link_libraries(VMSetup PRIVATE
    wininet shell32 user32 advapi32 ole32 gdi32
)

# Request admin elevation via manifest
set_target_properties(VMSetup PROPERTIES
    LINK_FLAGS "/MANIFEST:EMBED /MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    WIN32_EXECUTABLE FALSE  # Console app
)
