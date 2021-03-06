cmake_minimum_required(VERSION 3.12)

if(NOT DEFINED CMAKE_CXX_FLAGS)
	set(CMAKE_CXX_FLAGS "-Wall -Wextra -march=native -fno-rtti -fopenmp" CACHE STRING "Flags used by the compiler during all build types.")
endif()
# build type defaults to release
if(NOT DEFINED CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS) Debug Release RelWithdebInfo MinSizeRel.")
endif()

project(dangerousAssembly CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories("${CMAKE_SOURCE_DIR}/include/")

# add COAT from submodule
include_directories("${CMAKE_SOURCE_DIR}/external/coat/include/")

# find asmjit
find_path(ASMJIT_INCLUDE_DIR
	asmjit/asmjit.h
	HINTS external/asmjit ENV ASMJIT_ROOT
	PATH_SUFFIXES src
)
find_library(ASMJIT_LIBRARIES
	asmjit
	HINTS external/asmjit ENV ASMJIT_ROOT
	PATH_SUFFIXES build
)
include_directories(${ASMJIT_INCLUDE_DIR})

# find asmjit_perf
find_path(ASMJIT_PERF_INCLUDE_DIR
	asmjit-utilities/perf/jitdump.h
	HINTS external/
)
find_library(ASMJIT_PERF_LIBRARIES
	asmjit_perf
	HINTS external/asmjit-utilities
	PATH_SUFFIXES perf/build
)
include_directories(${ASMJIT_PERF_INCLUDE_DIR})

# find llvm, quick&dirty
find_path(LLVM_INCLUDE_DIR
	llvm/IR/IRBuilder.h
	HINTS external/llvm/llvm/install ENV LLVM_ROOT
	PATH_SUFFIXES include
)
find_library(LLVM_LIBRARIES
	LLVM-7.1
	HINTS external/llvm/llvm/install ENV LLVM_ROOT
	PATH_SUFFIXES lib build/lib
)
include_directories(${LLVM_INCLUDE_DIR})


add_executable(sig18 src/main.cpp src/Relation.cpp src/Query.cpp)

# test programs
add_executable(filter src/tests/filter.cpp)
add_executable(equijoin src/tests/equijoin.cpp)
add_executable(equijoin_unique src/tests/equijoin_unique.cpp)
add_executable(semijoin src/tests/semijoin.cpp)

foreach(prog sig18 filter equijoin equijoin_unique semijoin)
	target_compile_definitions(${prog} PRIVATE "ENABLE_ASMJIT" PRIVATE "ENABLE_LLVMJIT")
	target_link_libraries(${prog} ${ASMJIT_LIBRARIES} ${LLVM_LIBRARIES})
endforeach()


option(DISABLE_OPENMP "disable parallelization" OFF)
if(DISABLE_OPENMP)
	target_compile_definitions(sig18 PRIVATE "DISABLE_OPENMP")
endif()
option(MORSELS "enable parallelization by using morsels" ON)
if(MORSELS AND NOT DISABLE_OPENMP)
	target_compile_definitions(sig18 PRIVATE "MORSELS")
else()
	message("morsels disabled")
endif()

option(REWRITE_EQUIVALENCE "enable rewriting using equivalences of equi-join" ON)
if(REWRITE_EQUIVALENCE)
	target_compile_definitions(sig18 PRIVATE "REWRITE_EQUIVALENCE")
endif()
option(REWRITE_ORDER "enable reordering of join predicates" ON)
if(REWRITE_ORDER)
	target_compile_definitions(sig18 PRIVATE "REWRITE_ORDER")
endif()
option(REWRITE_IDENTICALJOINS "enable removal of identical joins" ON)
if(REWRITE_IDENTICALJOINS)
	target_compile_definitions(sig18 PRIVATE "REWRITE_IDENTICALJOINS")
endif()
option(MINIMIZECOL "enable minimization of column representation" ON)
if(MINIMIZECOL)
	target_compile_definitions(sig18 PRIVATE "MINIMIZECOL")
endif()

option(MEASURE_TIME "enable time measurement per query" ON)
if(MEASURE_TIME)
	target_compile_definitions(sig18 PRIVATE "MEASURE_TIME")
endif()
option(PROFILING "enable perf profiling support" OFF)
if(PROFILING)
	target_compile_definitions(sig18 PRIVATE "PROFILING" PRIVATE "PROFILING_SOURCE")
	target_link_libraries(sig18 ${ASMJIT_PERF_LIBRARIES})
endif()

# debugging options
option(VERBOSE_PARSE "enable additional debug messages in the parser" OFF)
if(VERBOSE_PARSE)
	target_compile_definitions(sig18 PRIVATE "VERBOSE_PARSE")
endif()
option(QUIET "disable output per query" ON)
if(QUIET)
	target_compile_definitions(sig18 PRIVATE "QUIET")
endif()
