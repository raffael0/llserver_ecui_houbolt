Include(FetchContent)
### Testing
# These tests can use the Catch2-provided main

file(GLOB_RECURSE sources *.cpp)

add_executable(tests ${sources})
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0 # Replace with desired version
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(tests)