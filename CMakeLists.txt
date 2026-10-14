cmake_minimum_required(VERSION 3.23)
project(Something LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${CMAKE_PROJECT_NAME}
    src/main.c
    src/dat.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/includes
)

