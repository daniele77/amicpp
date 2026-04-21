cmake_minimum_required(VERSION 3.13)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
    cmake_policy(SET CMP0144 NEW)
endif()

project(amicpp VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(AMICPP_BUILD_EXAMPLES "Build amicpp examples" ON)
option(AMICPP_BUILD_DOCS "Build API documentation with Doxygen" OFF)

find_package(Boost 1.66 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

if(AMICPP_BUILD_DOCS)
    find_package(Doxygen QUIET)
    if(DOXYGEN_FOUND)
        set(AMICPP_DOXYFILE ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in
            ${AMICPP_DOXYFILE}
            @ONLY
        )

        add_custom_target(amicpp_docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${AMICPP_DOXYFILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    else()
        message(WARNING "AMICPP_BUILD_DOCS=ON but Doxygen was not found")
    endif()
endif()

add_library(amicpp
    src/ami_message.cpp
    src/tcp_client.cpp
    src/ami_client.cpp
    src/ami_session.cpp
)

add_library(amicpp::amicpp ALIAS amicpp)

target_compile_features(amicpp PUBLIC cxx_std_14)

target_include_directories(amicpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(amicpp
    PUBLIC Boost::system
    PRIVATE Threads::Threads
)

if(AMICPP_BUILD_EXAMPLES)
    add_executable(amicpp_basic_client examples/basic_client.cpp)
    target_link_libraries(amicpp_basic_client PRIVATE amicpp::amicpp)

    add_executable(amicpp_async_client examples/async_client.cpp)
    target_link_libraries(amicpp_async_client PRIVATE amicpp::amicpp)

    add_executable(amicpp_sync_client examples/sync_client.cpp)
    target_link_libraries(amicpp_sync_client PRIVATE amicpp::amicpp)
endif()

install(TARGETS amicpp
    EXPORT amicppTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT amicppTargets
    FILE amicppTargets.cmake
    NAMESPACE amicpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/amicpp
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/amicppConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/amicppConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/amicppConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/amicpp
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/amicppConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/amicppConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/amicpp
)
