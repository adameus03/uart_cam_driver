cmake_minimum_required(VERSION 3.10)
project(ucd)
set(CMAKE_C_STANDARD 11)
set(BUILD_SHARED_LIBS OFF)

set(CMAKE_BUILD_TYPE Debug) # For debugging

#option(LITTLE_ENDIAN "Little endian" ON)
include (TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
if(IS_BIG_ENDIAN)
 message(STATUS "Target is BIG ENDIAN")
else()
 message(STATUS "Target is LITTLE ENDIAN")
endif()
if(IS_BIG_ENDIAN)
    add_definitions(-DTARGET_BIG_ENDIAN) # For big endian
else()
    add_definitions(-DTARGET_LITTLE_ENDIAN) # For little endian
endif()
find_package(Threads REQUIRED)
add_executable(${CMAKE_PROJECT_NAME} 
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/driver.c
)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)