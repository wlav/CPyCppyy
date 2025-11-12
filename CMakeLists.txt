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

set(cppyy_src
    src/API.cxx
    src/CPPClassMethod.cxx
    src/CPPConstructor.cxx
    src/CPPDataMember.cxx
    src/CPPEnum.cxx
    src/CPPExcInstance.cxx
    src/CPPFunction.cxx
    src/CPPGetSetItem.cxx
    src/CPPInstance.cxx
    src/CPPMethod.cxx
    src/CPPOperator.cxx
    src/CPPOverload.cxx
    src/CPPScope.cxx
    src/CPyCppyyModule.cxx
    src/CallContext.cxx
    src/Converters.cxx
    src/CustomPyTypes.cxx
    src/DispatchPtr.cxx
    src/Dispatcher.cxx
    src/Executors.cxx
    src/LowLevelViews.cxx
    src/MemoryRegulator.cxx
    src/ProxyWrappers.cxx
    src/PyException.cxx
    src/PyResult.cxx
    src/PyStrings.cxx
    src/Pythonize.cxx
    src/TPyArg.cxx
    # src/TPyClassGenerator.cxx # TODO: not sure if this still makes sense
    src/TemplateProxy.cxx
    src/TupleOfInstances.cxx
    src/TypeManip.cxx
    src/Utility.cxx
)

add_library(cppyy SHARED ${cppyy_src})

if(APPLE)
  set_target_properties(cppyy PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
  set_target_properties(cppyy PROPERTIES SUFFIX ".so")
endif()
