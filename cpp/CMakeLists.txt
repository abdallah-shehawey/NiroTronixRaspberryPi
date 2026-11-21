cmake_minimum_required(VERSION 3.16)
project(NiroTronixMotor VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

add_library(nirotronix_motor INTERFACE)
add_library(NiroTronix::Motor ALIAS nirotronix_motor)
set_target_properties(nirotronix_motor PROPERTIES EXPORT_NAME Motor)
target_compile_features(nirotronix_motor INTERFACE cxx_std_17)
target_include_directories(nirotronix_motor INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(NIROTRONIX_BUILD_DEMO "Build the motor control demo" ON)
endif()

if(NIROTRONIX_BUILD_DEMO)
    add_executable(motor_demo motor_demo.cpp)
    target_link_libraries(motor_demo PRIVATE NiroTronix::Motor)
endif()

install(TARGETS nirotronix_motor EXPORT NiroTronixMotorTargets)
install(FILES nirotronix_motor.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT NiroTronixMotorTargets
    FILE NiroTronixMotorConfig.cmake
    NAMESPACE NiroTronix::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/NiroTronixMotor"
)
