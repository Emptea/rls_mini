cmake_minimum_required(VERSION 3.16)

project(rls_mini LANGUAGES C CXX)

find_package(Git QUIET)

set(GIT_TAG "untagged")
set(GIT_HASH "unknown")

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_TAG_RESULT
    )

    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_HASH_RESULT
    )

    if(NOT GIT_TAG_RESULT EQUAL 0)
        set(GIT_TAG "untagged")
    endif()

    if(NOT GIT_HASH_RESULT EQUAL 0)
        set(GIT_HASH "unknown")
    endif()
endif()

set(APP_GIT_VERSION "${GIT_TAG}-${GIT_HASH}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/git_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/git_version.h"
    @ONLY
)

message(STATUS "Git version: ${APP_GIT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Install prefix" FORCE)

find_package(PIP REQUIRED)
find_package(UHD 4.1.0 REQUIRED)

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
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_version.h"
)

set_deploy_property(${PROJECT_NAME}
    LABEL "${PROJECT_NAME}"
    FULLNAME "rls_mini"
    COMPANY "SM5"
    INFO ""
)

make_rc(${PROJECT_NAME} out_RC)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../rls_fpga"
    "${CMAKE_CURRENT_BINARY_DIR}/rls_fpga"
)

file(GLOB CPPS CONFIGURE_DEPENDS "*.cpp")
file(GLOB HDRS CONFIGURE_DEPENDS "*.h")

pip_code_model(
    CCM_PROTOCOL "protocol_rls_mini.h"
    OPTIONS "-SEMs"
    NAME "ccm_protocol"
)

add_executable(${PROJECT_NAME}
    ${CPPS}
    ${HDRS}
    ${out_RC}
    ${CCM_PROTOCOL}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${Boost_INCLUDE_DIRS}
    ${UHD_INCLUDE_DIRS}
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${Boost_LIBRARY_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    PIP::Console
    PIP::IOUtils
    rls_fpga
)

if(NOT UHD_USE_STATIC_LIBS)
    message(STATUS "Linking against shared UHD library.")

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${UHD_LIBRARIES}
        ${Boost_LIBRARIES}
    )
else()
    message(STATUS "Linking against static UHD library.")

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${UHD_STATIC_LIB_LINK_FLAG}
        ${UHD_STATIC_LIB_DEPS}
        ${Boost_LIBRARIES}
    )
endif()

install(
    TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

deploy_target(${PROJECT_NAME}
    DEPLOY_DIR "${CMAKE_INSTALL_PREFIX}/bin"
    DESTINATION "${CMAKE_INSTALL_PREFIX}/release"
)
