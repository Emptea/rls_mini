cmake_minimum_required(VERSION 3.16)
project(rls_mini)
find_package(PIP REQUIRED)
set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/..)

set_version(${PROJECT_NAME}
	MAJOR 0
	MINOR 0
	REVISION 1
	BUILD ${BUILD_NUMBER}
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_version.h")
set_deploy_property(${PROJECT_NAME}
	LABEL "${PROJECT_NAME}"
	FULLNAME "rat"
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
