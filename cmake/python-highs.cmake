if (NOT PYTHON_BUILD_SETUP)
  return()
endif()

# set(CMAKE_VERBOSE_MAKEFILE ON)

include(sources-python)

set(sources_python ${highs_sources_python} 
                   ${cupdlp_sources_python} 
                   ${ipx_sources_python} 
                   ${basiclu_sources_python})

set(headers_python ${highs_headers_python} 
                   ${cupdlp_headers_python} 
                   ${ipx_headers_python} 
                   ${basiclu_headers_python})

# Find Python 3
find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)

# Locate nanobind's CMake config via the installed Python package
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
find_package(nanobind CONFIG REQUIRED)

# add module
nanobind_add_module(_core highs/highs_bindings.cpp)

# sources for python
target_sources(_core PUBLIC ${sources_python} ${headers_python})

# include directories for python 
target_include_directories(_core PUBLIC ${include_dirs_python})

# This is passing in the version as a define just as an example
target_compile_definitions(_core PRIVATE VERSION_INFO=${PROJECT_VERSION})

if(MSVC)
  target_compile_options(_core PRIVATE "/bigobj")
endif()

if (NOT MSVC) 
  target_compile_options(_core PRIVATE "-ftemplate-depth=2048")
endif()

# if(MSVC)
#   # Try to split large pdb files into objects. 
#   # https://github.com/tensorflow/tensorflow/issues/31610
#   add_compile_options("/Z7")
#   add_link_options("/DEBUG:FASTLINK")
#   if(STDCALL)
#     # /Gz - stdcall calling convention
#     add_definitions(/Gz)
#   endif()
# endif()

# The install directory is the output (wheel) directory
install(TARGETS _core DESTINATION highspy)
