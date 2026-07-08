cmake_minimum_required(VERSION 3.16)
project(rls_mini)
find_package(PIP REQUIRED)
find_package(UHD 4.1.0 REQUIRED)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(UHD_BOOST_REQUIRED_COMPONENTS
    program_options
    system
    thread
)
set(BOOST_MIN_VERSION 1.65)
include(UHDBoost)

set_version(${PROJECT_NAME}
	MAJOR 0
	MINOR 0
	REVISION 1
	BUILD ${BUILD_NUMBER}
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_version.h")
set_deploy_property(${PROJECT_NAME}
	LABEL "${PROJECT_NAME}"
	FULLNAME "rls_mini"
	COMPANY "SM5"
	INFO "")
make_rc(${PROJECT_NAME} out_RC)

file(GLOB CPPS "*.cpp")
file(GLOB HDRS "*.h")
pip_code_model(CCM_PROTOCOL "protocol_rls_mini.h" OPTIONS "-SEMs" NAME "ccm_protocol")
add_executable(${PROJECT_NAME} ${CPPS} ${HDRS} ${out_RC} ${CCM_PROTOCOL})
target_link_libraries(${PROJECT_NAME} PIP::Console PIP::IOUtils)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
deploy_target(${PROJECT_NAME}
	DEPLOY_DIR ${CMAKE_INSTALL_PREFIX}/bin
	DESTINATION ${CMAKE_INSTALL_PREFIX}/release)

# need these include and link directories for the build
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${UHD_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
link_directories(${Boost_LIBRARY_DIRS})

# Shared library case: All we need to do is link against the library, and
# anything else we need (in this case, some Boost libraries):
if(NOT UHD_USE_STATIC_LIBS)
    message(STATUS "Linking against shared UHD library.")
    target_link_libraries(${PROJECT_NAME} ${UHD_LIBRARIES} ${Boost_LIBRARIES})
# Shared library case: All we need to do is link against the library, and
# anything else we need (in this case, some Boost libraries):
else(NOT UHD_USE_STATIC_LIBS)
    message(STATUS "Linking against static UHD library.")
    target_link_libraries(test_loopback
        # We could use ${UHD_LIBRARIES}, but linking requires some extra flags,
        # so we use this convenience variable provided to us
        ${UHD_STATIC_LIB_LINK_FLAG}
        # Also, when linking statically, we need to pull in all the deps for
        # UHD as well, because the dependencies don't get resolved automatically
        ${UHD_STATIC_LIB_DEPS}
    )
endif(NOT UHD_USE_STATIC_LIBS)

