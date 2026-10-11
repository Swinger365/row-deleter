# Cross-compilation to 64-bit Windows with llvm-mingw or a GCC mingw-w64 toolchain.
#   cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
#         -DMINGW_ROOT=/path/to/llvm-mingw
# Without MINGW_ROOT the compilers are looked up in PATH.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_ROOT "" CACHE PATH "Root of the mingw-w64 toolchain (contains bin/)")
set(_triple x86_64-w64-mingw32)

if(MINGW_ROOT)
    set(_bin "${MINGW_ROOT}/bin/")
endif()

if(EXISTS "${_bin}${_triple}-clang++" OR NOT EXISTS "${_bin}${_triple}-g++")
    set(CMAKE_C_COMPILER   "${_bin}${_triple}-clang")
    set(CMAKE_CXX_COMPILER "${_bin}${_triple}-clang++")
else()
    set(CMAKE_C_COMPILER   "${_bin}${_triple}-gcc")
    set(CMAKE_CXX_COMPILER "${_bin}${_triple}-g++")
endif()
set(CMAKE_RC_COMPILER "${_bin}${_triple}-windres")

if(MINGW_ROOT)
    set(CMAKE_FIND_ROOT_PATH "${MINGW_ROOT}/${_triple}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# A self-contained .exe: no libc++/libstdc++/winpthread DLLs required next to it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
