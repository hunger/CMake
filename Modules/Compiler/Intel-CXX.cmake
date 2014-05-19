set(CMAKE_CXX_VERBOSE_FLAG "-v")

set(CMAKE_CXX_FLAGS_INIT "")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "-g")
set(CMAKE_CXX_FLAGS_MINSIZEREL_INIT "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-O2 -g -DNDEBUG")

set(CMAKE_DEPFILE_FLAGS_CXX "-MMD -MT <OBJECT> -MF <DEPFILE>")

set(CMAKE_CXX_CREATE_PREPROCESSED_SOURCE "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -E <SOURCE> > <PREPROCESSED_SOURCE>")
set(CMAKE_CXX_CREATE_ASSEMBLY_SOURCE "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -S <SOURCE> -o <ASSEMBLY_SOURCE>")

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14.0)
  return()
endif()

if (NOT UNIX OR APPLE OR WIN32)
  return()
endif()

set(CMAKE_CXX98_STANDARD_COMPILE_OPTION "-std=c++98")
set(CMAKE_CXX98_EXTENSION_COMPILE_OPTION "-std=gnu++98")

set(CMAKE_CXX11_STANDARD_COMPILE_OPTION "-std=c++11")
set(CMAKE_CXX11_EXTENSION_COMPILE_OPTION "-std=gnu++11")

set(CMAKE_CXX_STANDARD_DEFAULT 98)

macro(cmake_record_cxx_compile_features)
  macro(_get_intel_features std_version list)
    record_compiler_features(CXX "-std=${std_version}" ${list})
  endmacro()

  _get_intel_features(c++11 CMAKE_CXX11_COMPILE_FEATURES)
  if (_result EQUAL 0)
    _get_intel_features(c++98 CMAKE_CXX98_COMPILE_FEATURES)
  endif()
endmacro()
