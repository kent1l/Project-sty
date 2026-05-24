cmake_minimum_required(VERSION 3.14)
project(ArrangerEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch RtMidi for Live Chord Recognition
include(FetchContent)
FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(rtmidi)

# Include directories
include_directories(src)

# Source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Executable
add_executable(ArrangerEngine ${SOURCES})

# Link libraries
target_link_libraries(ArrangerEngine PRIVATE rtmidi::rtmidi)
if(WIN32)
    target_link_libraries(ArrangerEngine PRIVATE ws2_32)
endif()
