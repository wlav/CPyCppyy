cmake_minimum_required(VERSION 3.4.3 FATAL_ERROR)
project (cppyy)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wno-strict-aliasing -Wno-register")
if (CMAKE_COMPILER_IS_GNUCXX)
   set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-Bsymbolic-functions")
endif()

message(STATUS "Looking for python interpreter")
#---First look for the python interpreter and fix the version of it for the libraries--
find_package(PythonInterp)

if (PYTHONINTERP_FOUND)
   execute_process(COMMAND ${PYTHON_EXECUTABLE} -c "import sys;sys.stdout.write(sys.version[:3])"
                   OUTPUT_VARIABLE PYTHON_VERSION)
   execute_process(COMMAND ${PYTHON_EXECUTABLE} -c "import sys;sys.stdout.write(sys.prefix)"
                   OUTPUT_VARIABLE PYTHON_PREFIX)
   set (CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${PYTHON_PREFIX})
endif()

set(Python_ADDITIONAL_VERSIONS ${PYTHON_VERSION})
find_package(PythonLibs REQUIRED)

if (NOT PYTHONLIBS_FOUND)
   message(FATAL_ERROR "PythonLibs package not found and python component required")
endif()

include_directories("${PROJECT_SOURCE_DIR}/include" "${PYTHON_INCLUDE_PATH}")

file (GLOB cppyy_src src/*.cxx)

add_library(cppyy SHARED ${cppyy_src})

if(APPLE)
  set_target_properties(cppyy PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
  set_target_properties(cppyy PROPERTIES SUFFIX ".so")
endif()
