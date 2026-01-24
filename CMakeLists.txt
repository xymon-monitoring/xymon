cmake_minimum_required(VERSION 3.16)

project(xymon C)

# ---- Options minimales ----
set(XYMON_VARIANT "server" CACHE STRING "Build variant (server or client)")

option(ENABLE_SSL "Enable SSL support" ON)
option(ENABLE_LDAP "Enable LDAP support" ON)

# ---- Détection plateforme ----
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER}")

# ---- Génération de config.h ----
configure_file(
    ${CMAKE_SOURCE_DIR}/config.h.in
    ${CMAKE_BINARY_DIR}/config.h
)

# ---- Dépendances ----
find_library(CARES_LIB cares REQUIRED)
find_path(CARES_INCLUDE ares.h REQUIRED)

find_library(PCRE_LIB pcre REQUIRED)
find_path(PCRE_INCLUDE pcre.h REQUIRED)

if(ENABLE_SSL)
    find_package(OpenSSL REQUIRED)
endif()

if(ENABLE_LDAP)
    find_library(LDAP_LIB ldap REQUIRED)
    find_path(LDAP_INCLUDE ldap.h REQUIRED)
endif()

# ---- Définitions ----
if(XYMON_VARIANT STREQUAL "server")
    add_compile_definitions(XYMON_SERVER)
elseif(XYMON_VARIANT STREQUAL "client")
    add_compile_definitions(XYMON_CLIENT)
else()
    message(FATAL_ERROR "Invalid XYMON_VARIANT: ${XYMON_VARIANT}")
endif()

# ---- Bibliothèque commune réelle ----
add_library(xymon_common STATIC
    lib/errormsg.c
    lib/tree.c
    lib/environ.c
    lib/memory.c
    lib/timefunc.c
    lib/matching.c
    lib/locator.c
    lib/md5.c
)

target_include_directories(xymon_common PUBLIC
    include
    lib
    ${CMAKE_BINARY_DIR}
    ${CARES_INCLUDE}
    ${PCRE_INCLUDE}
    ${LDAP_INCLUDE}
)

target_link_libraries(xymon_common
    ${CARES_LIB}
    ${PCRE_LIB}
    ${LDAP_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
)

# ---- Binaire : xymond_channel ----
add_executable(xymond_channel
    xymond/xymond_channel.c
)

target_include_directories(xymond_channel PRIVATE
    xymond
)

target_link_libraries(xymond_channel
    xymon_common
)

