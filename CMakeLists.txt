cmake_minimum_required(VERSION 3.16)

project(xymon C)

set(XYMON_VARIANT "server" CACHE STRING "Build variant (server or client)")

message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER}")

configure_file(
    ${CMAKE_SOURCE_DIR}/config.h.in
    ${CMAKE_BINARY_DIR}/config.h
)

if(XYMON_VARIANT STREQUAL "server")
    add_compile_definitions(XYMON_SERVER)
elseif(XYMON_VARIANT STREQUAL "client")
    add_compile_definitions(XYMON_CLIENT)
else()
    message(FATAL_ERROR "Invalid XYMON_VARIANT: ${XYMON_VARIANT}")
endif()

# ---- xymon_common: shared utilities + loaders (B6.5: not isolable yet) ----
add_library(xymon_common STATIC
    lib/errormsg.c
    lib/tree.c
    lib/memory.c
    lib/md5.c
    lib/strfunc.c
    lib/timefunc.c
    lib/digest.c
    lib/encoding.c
    lib/calc.c
    lib/misc.c
    lib/msort.c
    lib/files.c
    lib/stackio.c
    lib/sig.c
    lib/suid.c
    lib/xymond_buffer.c
    lib/xymond_ipc.c
    lib/matching.c
    lib/timing.c
    lib/crondate.c

    # loaders (still shared in B6.5)
    lib/loadhosts.c
    lib/loadhosts_file.c
    lib/loadalerts.c
    lib/loadcriticalconf.c
)

target_include_directories(xymon_common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_BINARY_DIR}
)

target_compile_options(xymon_common PRIVATE
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -Werror=implicit-int
)

# ---- xymon_server_core: server-only, clean boundary (logs only) ----
add_library(xymon_server_core STATIC
    lib/server_stub.c
    lib/eventlog.c
    lib/notifylog.c
    lib/htmllog.c
    lib/reportlog.c
)

target_include_directories(xymon_server_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_BINARY_DIR}
)

add_executable(xymond_channel
    cmake/smoke.c
)

target_link_libraries(xymond_channel
    xymon_common
    xymon_server_core
)

