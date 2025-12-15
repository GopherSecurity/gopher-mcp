# MinGW-w64 Cross-Compilation Toolchain for Cygwin
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64-toolchain.cmake ..

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Specify the cross compilers
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc.exe)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++.exe)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres.exe)
set(CMAKE_AR x86_64-w64-mingw32-ar.exe)
set(CMAKE_RANLIB x86_64-w64-mingw32-ranlib.exe)

# MinGW sysroot paths
set(MINGW_SYSROOT /usr/x86_64-w64-mingw32/sys-root/mingw)

# Target environment - Cygwin's MinGW sysroot
set(CMAKE_FIND_ROOT_PATH
    ${MINGW_SYSROOT}
    /usr/x86_64-w64-mingw32
)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# OpenSSL paths for MinGW
set(OPENSSL_ROOT_DIR ${MINGW_SYSROOT})
set(OPENSSL_INCLUDE_DIR ${MINGW_SYSROOT}/include)
set(OPENSSL_CRYPTO_LIBRARY ${MINGW_SYSROOT}/lib/libcrypto.dll.a)
set(OPENSSL_SSL_LIBRARY ${MINGW_SYSROOT}/lib/libssl.dll.a)

# yaml-cpp paths for MinGW
set(yaml-cpp_DIR ${MINGW_SYSROOT}/lib/cmake/yaml-cpp)
set(YAML_CPP_INCLUDE_DIR ${MINGW_SYSROOT}/include)
set(YAML_CPP_LIBRARIES ${MINGW_SYSROOT}/lib/libyaml-cpp.dll.a)

# libevent paths for MinGW
set(LIBEVENT_INCLUDE_DIRS ${MINGW_SYSROOT}/include)
set(LIBEVENT_LIBRARIES
    ${MINGW_SYSROOT}/lib/libevent.dll.a
    ${MINGW_SYSROOT}/lib/libevent_core.dll.a
)

# fmt library paths
set(fmt_DIR ${MINGW_SYSROOT}/lib/cmake/fmt)

# pkg-config path for MinGW packages
set(ENV{PKG_CONFIG_PATH} "${MINGW_SYSROOT}/lib/pkgconfig")
set(PKG_CONFIG_EXECUTABLE /usr/bin/x86_64-w64-mingw32-pkg-config)

# Additional include/library paths
set(CMAKE_INCLUDE_PATH ${MINGW_SYSROOT}/include)
set(CMAKE_LIBRARY_PATH ${MINGW_SYSROOT}/lib)
set(CMAKE_PREFIX_PATH ${MINGW_SYSROOT})

# Windows platform definitions
set(WIN32 TRUE)
set(MINGW TRUE)
add_definitions(-D_WIN32 -DWIN32 -D_WINDOWS -DMINGW)

# Ensure Windows socket libraries are linked
link_libraries(ws2_32 mswsock)

# Disable features that may cause issues with cross-compilation
set(CMAKE_CROSSCOMPILING TRUE)

# Static linking of libgcc and libstdc++ for easier distribution
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
