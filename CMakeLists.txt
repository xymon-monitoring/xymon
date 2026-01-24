cmake_minimum_required(VERSION 3.16)

project(xymon C)

# ---- Options (OPTION B - B3) ----
set(XYMON_VARIANT "server" CACHE STRING "Build variant (server or client)")
# option(ENABLE_SSL "Enable SSL support" ON)
# option(ENABLE_LDAP "Enable LDAP support" ON)

# ---- Detection plateforme ----
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER}")

# ---- Generation minimale de config.h ----
configure_file(
    ${CMAKE_SOURCE_DIR}/config.h.in
    ${CMAKE_BINARY_DIR}/config.h
)

# ---- Definitions (OPTION B - B3) ----
if(XYMON_VARIANT STREQUAL "server")
    add_compile_definitions(XYMON_SERVER)
elseif(XYMON_VARIANT STREQUAL "client")
    add_compile_definitions(XYMON_CLIENT)
else()
    message(FATAL_ERROR "Invalid XYMON_VARIANT: ${XYMON_VARIANT}")
endif()

# ---- Bibliotheque commune (OPTION B - continue sources, grouped) ----
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
)

target_include_directories(xymon_common PUBLIC
    include
    lib
    ${CMAKE_BINARY_DIR}
)

# ---- Garde-fou minimal C ----
target_compile_options(xymon_common PRIVATE
    -Werror=implicit-function-declaration
    -Werror=implicit-int
)

# ---- Binaire smoke-test ----
add_executable(xymond_channel
    cmake/smoke.c
)

target_link_libraries(xymond_channel
    xymon_common
)

