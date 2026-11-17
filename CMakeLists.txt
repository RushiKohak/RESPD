cmake_minimum_required(VERSION 3.16)
project(Respd VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

set(RESPD_SOURCES
    main_server.cpp
    server.cpp
    db.cpp
    resp.cpp
    value.cpp
)

if(WIN32)
    list(APPEND RESPD_SOURCES wepoll.c)
endif()

add_executable(respd ${RESPD_SOURCES})

if(WIN32)
    target_link_libraries(respd PRIVATE ws2_32)
else()
    target_link_libraries(respd PRIVATE pthread)
endif()
