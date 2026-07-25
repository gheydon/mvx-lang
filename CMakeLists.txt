cmake_minimum_required(VERSION 3.24)
project(mvx LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The runtime uses glibc extensions (memmem, dladdr).  Without _GNU_SOURCE
# glibc leaves them undeclared, so C89 implicit-declaration rules make them
# return int — truncating the returned 64-bit pointer to a crash.  Set it
# tree-wide; it is a no-op on macOS, where these live in the base headers.
add_compile_definitions(_GNU_SOURCE)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")

# --- runtime -----------------------------------------------------------
# A SHARED library so its code is mapped once and shared across every mvx
# process (each cataloged verb is its own executable), and so dlopen'd
# subroutine libraries and storage drivers resolve mv_*/mvx_* from the
# single loaded copy — no more force-loading a static archive into each
# executable.
add_library(mvxrt SHARED
  runtime/src/mv_value.c
  runtime/src/mv_dyn.c
  runtime/src/mv_str.c
  runtime/src/mv_conv.c
  runtime/src/mvx_ctx.c
  runtime/src/mvx_store.c
  runtime/src/mvx_exec.c
  runtime/src/mvx_cred.c
  runtime/src/mvx_conn.c
  runtime/src/mvx_call.c
  runtime/src/mvx_term.c
  runtime/src/mvx_os.c
  runtime/src/mvx_acct.c)
target_include_directories(mvxrt PUBLIC runtime/include)
# The runtime calls libm (sin/pow/sqrt/... for the numeric intrinsics);
# glibc keeps libm separate, so link it explicitly (a no-op on macOS,
# where it lives in libSystem).
find_library(M_LIBRARY m)
if(M_LIBRARY)
  target_link_libraries(mvxrt PRIVATE ${M_LIBRARY})
endif()
target_compile_definitions(mvxrt PRIVATE
  MVX_DRIVER_DIR="${CMAKE_BINARY_DIR}/lib"
  MVX_BIN_DIR="${CMAKE_BINARY_DIR}/bin"
  MVX_SYSTEM_DIR="${CMAKE_BINARY_DIR}/system")
set_target_properties(mvxrt PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- storage drivers: dlopen'd shared libraries ------------------------
# Each driver links its own backend dependency; compiled programs link
# only libmvxrt.  Runtime symbols (mv_*, mvx_*) resolve from the host
# process at load time.
function(mvx_driver name)
  add_library(mvxdrv_${name} SHARED ${ARGN})
  target_include_directories(mvxdrv_${name} PRIVATE runtime/include)
  set_target_properties(mvxdrv_${name} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    PREFIX "lib")
  if(APPLE)
    target_link_options(mvxdrv_${name} PRIVATE -undefined dynamic_lookup)
  endif()
endfunction()

mvx_driver(dir runtime/src/mvx_drv_dir.c)

find_path(LMDB_INCLUDE_DIR lmdb.h REQUIRED)
find_library(LMDB_LIBRARY lmdb REQUIRED)
mvx_driver(lmdb runtime/src/mvx_drv_lmdb.c)
target_include_directories(mvxdrv_lmdb PRIVATE ${LMDB_INCLUDE_DIR})
target_link_libraries(mvxdrv_lmdb PRIVATE ${LMDB_LIBRARY})

mvx_driver(lmdbnet runtime/src/mvx_drv_lmdbnet.c)

# postgres driver — optional, built only when libpq is present.  libpq
# is keg-only on Homebrew, so also probe its opt prefix.
find_path(LIBPQ_INCLUDE_DIR libpq-fe.h
  HINTS /opt/homebrew/opt/libpq/include /usr/local/opt/libpq/include
        /usr/include/postgresql)
find_library(LIBPQ_LIBRARY pq
  HINTS /opt/homebrew/opt/libpq/lib /usr/local/opt/libpq/lib)
if(LIBPQ_INCLUDE_DIR AND LIBPQ_LIBRARY)
  message(STATUS "Building postgres driver against ${LIBPQ_LIBRARY}")
  mvx_driver(postgres runtime/src/mvx_drv_postgres.c)
  target_include_directories(mvxdrv_postgres PRIVATE ${LIBPQ_INCLUDE_DIR})
  target_link_libraries(mvxdrv_postgres PRIVATE ${LIBPQ_LIBRARY})
else()
  message(STATUS "libpq not found — skipping postgres driver")
endif()

# --- the networked LMDB daemon -----------------------------------------
add_executable(mvx-lmdbd daemon/mvxd.c daemon/sha256.c)
target_include_directories(mvx-lmdbd PRIVATE ${LMDB_INCLUDE_DIR})
target_link_libraries(mvx-lmdbd PRIVATE ${LMDB_LIBRARY})
set_target_properties(mvx-lmdbd PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# mvx-lmdbd-admin: standalone credential provisioning for the daemon host
# (libc + vendored SHA-256 only — no MVX runtime, no LMDB).
add_executable(mvx-lmdbd-admin daemon/mvxd_admin.c daemon/sha256.c)
set_target_properties(mvx-lmdbd-admin PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- mvx-convert-acct: bootstrap tool for plain-git checkouts ----------
add_executable(mvx-convert-acct tools/mvx_convert_acct.c)
target_link_libraries(mvx-convert-acct PRIVATE mvxrt)
set_target_properties(mvx-convert-acct PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- classic TCL shell -------------------------------------------------
add_executable(mvx tcl/mvx_tcl.c)
target_link_libraries(mvx PRIVATE mvxrt ${CMAKE_DL_LIBS})
find_library(EDITLINE_LIBRARY edit)
if(EDITLINE_LIBRARY)
  target_compile_definitions(mvx PRIVATE HAVE_EDITLINE)
  target_link_libraries(mvx PRIVATE ${EDITLINE_LIBRARY})
endif()
target_compile_definitions(mvx PRIVATE
  MVX_SYSTEM_DIR="${CMAKE_BINARY_DIR}/system")
set_target_properties(mvx PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- system account: the source of truth for standard verbs ------------
# Master VOC lives in the repo as a directory file (system/VOC, one text
# record per verb); the build compiles verbs/*.b into system/CATALOG.
# Accounts hold only local VOC entries and fall back to this.
set(MVX_SYS_DIR ${CMAKE_BINARY_DIR}/system)
file(GLOB MVX_VERB_SOURCES ${CMAKE_SOURCE_DIR}/verbs/*.b)
set(MVX_SYS_VERBS "")
foreach(vsrc ${MVX_VERB_SOURCES})
  get_filename_component(vname ${vsrc} NAME_WE)
  add_custom_command(OUTPUT ${MVX_SYS_DIR}/CATALOG/${vname}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MVX_SYS_DIR}/CATALOG
    COMMAND $<TARGET_FILE:mvx-basic> ${vsrc} -o ${MVX_SYS_DIR}/CATALOG/${vname}
    DEPENDS mvx-basic mvxrt mvx_crt mvxdrv_dir mvxdrv_lmdb ${vsrc}
    COMMENT "Cataloging ${vname}")
  list(APPEND MVX_SYS_VERBS ${MVX_SYS_DIR}/CATALOG/${vname})
endforeach()
add_custom_target(system-account ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory ${MVX_SYS_DIR}/VOC
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/system/VOC ${MVX_SYS_DIR}/VOC
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/system/VOC.DICT ${MVX_SYS_DIR}/VOC.DICT
  DEPENDS ${MVX_SYS_VERBS})

# --- tests -------------------------------------------------------------
add_custom_target(check
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/test.sh
  DEPENDS mvx-basic mvx system-account mvxdrv_dir mvxdrv_lmdb mvx_crt
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  USES_TERMINAL)

# Program-entry object, shipped standalone so shared-library builds never
# pick up a main().  The driver links it explicitly for executables.
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/lib/mvx_crt.o
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
  COMMAND ${CMAKE_C_COMPILER} -std=c11 -O2
          -I${CMAKE_SOURCE_DIR}/runtime/include
          -c ${CMAKE_SOURCE_DIR}/runtime/src/mvx_crt.c
          -o ${CMAKE_BINARY_DIR}/lib/mvx_crt.o
  DEPENDS runtime/src/mvx_crt.c runtime/include/mvx_runtime.h)
add_custom_target(mvx_crt ALL DEPENDS ${CMAKE_BINARY_DIR}/lib/mvx_crt.o)

# --- compiler ----------------------------------------------------------
add_executable(mvx-basic
  compiler/src/main.cpp
  compiler/src/preprocess.cpp
  compiler/src/lexer.cpp
  compiler/src/parser.cpp
  compiler/src/codegen.cpp)
target_include_directories(mvx-basic SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(mvx-basic PRIVATE ${LLVM_DEFINITIONS_LIST})
set_target_properties(mvx-basic PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_dependencies(mvx-basic mvxrt mvx_crt)

llvm_map_components_to_libnames(LLVM_LIBS
  core support passes native irreader targetparser)
target_link_libraries(mvx-basic PRIVATE ${LLVM_LIBS})

# --- install -----------------------------------------------------------
# A relocatable prefix: bin/ holds the executables, lib/ the shared
# runtime and dlopen'd drivers, share/mvx/system the standard verbs.
# Everything is located at run time relative to libmvxrt (mvx_runtime_dir),
# so the whole prefix can be moved as a unit.  Executables reach libmvxrt
# through a loader-relative rpath; the compiler and drivers keep resolving
# ../lib relative to their own path (already the case), and the verbs it
# catalogs get an rpath into this lib on install.  Layout is fixed to
# bin/lib (not GNUInstallDirs) so ../lib always matches the compiler's
# assumption on every platform.
if(APPLE)
  set(MVX_INSTALL_RPATH "@loader_path/../lib")
else()
  set(MVX_INSTALL_RPATH "\$ORIGIN/../lib")
endif()
set_target_properties(mvx mvx-basic mvx-convert-acct PROPERTIES
  INSTALL_RPATH "${MVX_INSTALL_RPATH}")

install(TARGETS mvx mvx-basic mvx-lmdbd mvx-lmdbd-admin mvx-convert-acct
        RUNTIME DESTINATION bin)
install(TARGETS mvxrt mvxdrv_dir mvxdrv_lmdb mvxdrv_lmdbnet
        LIBRARY DESTINATION lib)
install(FILES ${CMAKE_BINARY_DIR}/lib/mvx_crt.o DESTINATION lib)

set(MVX_SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(${CMAKE_SOURCE_DIR}/scripts/install-system.cmake.in
               ${CMAKE_BINARY_DIR}/install-system.cmake @ONLY)
install(SCRIPT ${CMAKE_BINARY_DIR}/install-system.cmake)
