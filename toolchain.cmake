set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR "arm64")
set(TRIPLET_PATH "aarch64-none-linux-gnu")
set(TRIPLET_TOOL "aarch64-linux-gnu")

set(SYSROOT_PATH "/chroots/${TRIPLET_PATH}")
set(CMAKE_SYSROOT "${SYSROOT_PATH}")
set(CMAKE_FIND_ROOT_PATH "${SYSROOT_PATH}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_CROSSCOMPILING_EMULATOR "/usr/bin/qemu-aarch64-static;-L;${SYSROOT_PATH}")

set(CMAKE_LIBRARY_ARCHITECTURE "${TRIPLET_TOOL}")
set(CMAKE_C_COMPILER "${SYSROOT_PATH}/bin/${TRIPLET_TOOL}-gcc")
set(CMAKE_CXX_COMPILER "${SYSROOT_PATH}/bin/${TRIPLET_TOOL}-g++")
set(CMAKE_AR "${SYSROOT_PATH}/bin/${TRIPLET_TOOL}-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "${SYSROOT_PATH}/bin/${TRIPLET_TOOL}-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_READELF "${SYSROOT_PATH}/bin/${TRIPLET_TOOL}-readelf" CACHE FILEPATH "Readelf")
set(CMAKE_AS "${SYSROOT_PATH}/bin/${TRIPLET_TOOL}-as" CACHE FILEPATH "Assembler")
set(CMAKE_LINKER "${SYSROOT_PATH}/bin/${TRIPLET_TOOL}-ld" CACHE FILEPATH "Linker")

set(CMAKE_GCC_INSTALL_DIR "${SYSROOT_PATH}/usr/lib/gcc/${TRIPLET_TOOL}/14")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17" CACHE STRING "" FORCE)

# Force the compiler to use the cross-assembler
set(CMAKE_C_FLAGS "-B${SYSROOT_PATH}/bin -Wa,--noexecstack" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-B${SYSROOT_PATH}/bin -Wa,--noexecstack" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -B /chroots/aarch64-none-linux-gnu/usr/lib/gcc/aarch64-linux-gnu/14")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -B /chroots/aarch64-none-linux-gnu/usr/lib/gcc/aarch64-linux-gnu/14")

set(CMAKE_EXE_LINKER_FLAGS "-Wl,-rpath-link,${SYSROOT_PATH}/usr/lib/aarch64-linux-gnu:${SYSROOT_PATH}/lib/aarch64-linux-gnu -L${SYSROOT_PATH}/usr/lib/aarch64-linux-gnu -L${SYSROOT_PATH}/lib/aarch64-linux-gnu" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" CACHE STRING "" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

list(APPEND CMAKE_PREFIX_PATH "${SYSROOT_PATH}")
list(APPEND CMAKE_PROGRAM_PATH 
    /chroots/aarch64-none-linux-gnu/usr/local/bin
    /chroots/aarch64-none-linux-gnu/usr/bin)

set(CMAKE_MODULE_PATH "${SYSROOT_PATH}/usr/share/cmake-3.22/Modules/")
set(PIP_DIR "${SYSROOT_PATH}/usr/local/include/pip/")
set(PIP_H_INCLUDE "${SYSROOT_PATH}/usr/local/include/pip/pip_version.h")
set(Boost_DIR "${SYSROOT_PATH}/usr/lib/aarch64-linux-gnu/cmake/Boost-1.83.0")
cmake_policy(SET CMP0025 NEW)
