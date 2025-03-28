Include(FetchContent)
### Testing
# These tests can use the Catch2-provided main

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED)
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall")

file(GLOB_RECURSE sources ${PROJECT_SOURCE_DIR}/src/*.cpp ${PROJECT_SOURCE_DIR}/src/*.c ${PROJECT_SOURCE_DIR}/include/*.h)

add_subdirectory(dependencies/trompeloeil)

include_directories(../include ../include/can_houbolt)


option(TEST "test with simple CAN Driver simulation" OFF)
if(TEST)
    add_compile_definitions(TEST_LLSERVER)
endif()

if(NO_CANLIB)
    MESSAGE(STATUS "Kvaser CanLib is not used")
    add_compile_definitions(NO_CANLIB)
endif()

if(NO_INFLUX)
    MESSAGE(STATUS "InfluxDB is not used")
    add_compile_definitions(NO_INFLUX)
endif()

if(NO_PYTHON)
    MESSAGE(STATUS "Python features are not used")
    add_compile_definitions(NO_PYTHON)
    list(REMOVE_ITEM sources ${PROJECT_SOURCE_DIR}/src/drivers/PythonController.cpp)
else()
    find_package(Python3 COMPONENTS Development REQUIRED)
    include_directories(${Python3_INCLUDE_DIRS})
endif()
add_compile_definitions(PROJECT_DIR="${PROJECT_SOURCE_DIR}")
MESSAGE(STATUS "${PROJECT_SOURCE_DIR}")

add_executable(tests ${sources})
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain trompeloeil::trompeloeil)
target_link_libraries(tests PRIVATE Threads::Threads -lstdc++fs -latomic)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.0 # Replace with desired version
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

catch_discover_tests(tests)