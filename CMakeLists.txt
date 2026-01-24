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

# ---- Dependances (DESACTIVEES) ----
# find_library(CARES_LIB cares REQUIRED)
# find_path(CARES_INCLUDE ares.h REQUIRED)
#
# find_library(PCRE_LIB pcre REQUIRED)
# find_path(PCRE_INCLUDE pcre.h REQUIRED)
#
# if(ENABLE_SSL)
#     find_package(OpenSSL REQUIRED)
# endif()
#
# if(ENABLE_LDAP)
#     find_library(LDAP_LIB ldap REQUIRED)
#     find_path(LDAP_INCLUDE ldap.h REQUIRED)
# endif()

# ---- Definitions (OPTION B - B3) ----
if(XYMON_VARIANT STREQUAL "server")
    add_compile_definitions(XYMON_SERVER)
elseif(XYMON_VARIANT STREQUAL "client")
    add_compile_definitions(XYMON_CLIENT)
else()
    message(FATAL_ERROR "Invalid XYMON_VARIANT: ${XYMON_VARIANT}")
endif()

# ---- Bibliotheque commune minimale (OPTION B - B4.2) ----
# Reactivation incrementale, sans dependances externes
add_library(xymon_common STATIC
    lib/errormsg.c
    lib/tree.c
    lib/memory.c
    lib/md5.c
    lib/strfunc.c
    lib/timefunc.c
)

target_include_directories(xymon_common PUBLIC
    include
    lib
    ${CMAKE_BINARY_DIR}
)

# ---- Binaire smoke-test (conserve) ----
add_executable(xymond_channel
    cmake/smoke.c
)

# ---- Lien minimal vers la lib commune (OPTION B - B2) ----
target_link_libraries(xymond_channel
    xymon_common
)

