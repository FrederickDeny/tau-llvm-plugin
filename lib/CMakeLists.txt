# For use with C programs
if(${CLANG_VERSION_MAJOR} VERSION_LESS 8)
add_llvm_loadable_module(TAU_Profiling
    TAUInstrument.cpp

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )
else()
add_llvm_library(TAU_Profiling
  MODULE
  TAUInstrument.cpp

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )
endif()

# For use with C++ programs
if(${CLANG_VERSION_MAJOR} VERSION_LESS 8)
add_llvm_loadable_module(TAU_Profiling_CXX
    TAUInstrument.cpp

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )
else()
add_llvm_library(TAU_Profiling_CXX
  MODULE
  TAUInstrument.cpp

  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
  )
endif()

target_compile_definitions(TAU_Profiling_CXX PUBLIC TAU_PROF_CXX)
