cmake_minimum_required(VERSION 3.24)
project(mvx LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

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
  runtime/src/mvx_call.c
  runtime/src/mvx_term.c
  runtime/src/mvx_os.c
  runtime/src/mvx_acct.c)
target_include_directories(mvxrt PUBLIC runtime/include)
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

# --- the networked LMDB daemon -----------------------------------------
add_executable(mvx-lmdbd daemon/mvxd.c)
target_include_directories(mvx-lmdbd PRIVATE ${LMDB_INCLUDE_DIR})
target_link_libraries(mvx-lmdbd PRIVATE ${LMDB_LIBRARY})
set_target_properties(mvx-lmdbd PROPERTIES
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
