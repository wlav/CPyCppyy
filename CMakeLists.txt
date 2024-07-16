cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project (cppyy)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wno-strict-aliasing -Wno-register")
if (CMAKE_COMPILER_IS_GNUCXX)
   set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-Bsymbolic-functions")
endif()

message(STATUS "Looking for python ...")
set(Python_FIND_VIRTUALENV ONLY)
find_package (Python COMPONENTS Interpreter Development)
if (NOT Python_FOUND)
   set(Python_FIND_VIRTUALENV STANDARD)
   find_package (Python COMPONENTS Interpreter Development)
endif()

if (NOT Python_Development_FOUND)
   message(FATAL_ERROR "Python development package not found and python component required")
endif()

include_directories("${PROJECT_SOURCE_DIR}/include" "${Python_INCLUDE_DIRS}")
include_directories("/Users/khushiyant/Desktop/Development/gsoc/.venv/lib/python3.10/site-packages/numpy/_core/include")

file (GLOB cppyy_src src/*.cxx)

add_library(cppyy SHARED ${cppyy_src})

if(APPLE)
  set_target_properties(cppyy PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
  set_target_properties(cppyy PROPERTIES SUFFIX ".so")
endif()
