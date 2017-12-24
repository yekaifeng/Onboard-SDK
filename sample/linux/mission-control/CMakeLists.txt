cmake_minimum_required(VERSION 2.8)
project(djiosdk-mission-control)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread -g -O0")

FIND_PACKAGE(Boost 1.65.1 COMPONENTS chrono system REQUIRED)
INCLUDE_DIRECTORIES(SYSTEM ${Boost_INCLUDE_DIRS})
LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})

SET(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../../Modules)
FIND_PACKAGE(Rabbitmqc REQUIRED)
INCLUDE_DIRECTORIES(SYSTEM ${Rabbitmqc_INCLUDE_DIRS})

option(BUILD_SHARED_LIBS "Build SimpleAmqpClient as a shared library" ON)


if(NOT SIMPLEAMQPCLIENT_SOURCE)
    set(SIMPLEAMQPCLIENT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../../SimpleAmqpClient")
endif()

# SimpleAmqpClient library files
SET(SAC_LIB_SRCS
        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/SimpleAmqpClient.h

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/AmqpException.h
        ${SIMPLEAMQPCLIENT_SOURCE}/AmqpException.cpp

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/Channel.h
        ${SIMPLEAMQPCLIENT_SOURCE}/Channel.cpp

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/ChannelImpl.h
        ${SIMPLEAMQPCLIENT_SOURCE}/ChannelImpl.cpp

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/BasicMessage.h
        ${SIMPLEAMQPCLIENT_SOURCE}/BasicMessage.cpp

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/Util.h

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/AmqpLibraryException.h
        ${SIMPLEAMQPCLIENT_SOURCE}/AmqpLibraryException.cpp

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/AmqpResponseLibraryException.h
        ${SIMPLEAMQPCLIENT_SOURCE}/AmqpResponseLibraryException.cpp

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/BadUriException.h
        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/ConnectionClosedException.h
        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/ConsumerTagNotFoundException.h

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/Envelope.h
        ${SIMPLEAMQPCLIENT_SOURCE}/Envelope.cpp

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/MessageReturnedException.h
        ${SIMPLEAMQPCLIENT_SOURCE}/MessageReturnedException.cpp

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/Table.h
        ${SIMPLEAMQPCLIENT_SOURCE}/Table.cpp

        ${SIMPLEAMQPCLIENT_SOURCE}/SimpleAmqpClient/TableImpl.h
        ${SIMPLEAMQPCLIENT_SOURCE}/TableImpl.cpp
        )


ADD_LIBRARY(SimpleAmqpClient ${SAC_LIB_SRCS})

# Jsoncpp library files
SET( JSONCPP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../jsoncpp/include" )
SET( JSONCPP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../jsoncpp/src/lib_json" )

SET( PUBLIC_HEADERS
        ${JSONCPP_INCLUDE_DIR}/json/config.h
        ${JSONCPP_INCLUDE_DIR}/json/forwards.h
        ${JSONCPP_INCLUDE_DIR}/json/features.h
        ${JSONCPP_INCLUDE_DIR}/json/value.h
        ${JSONCPP_INCLUDE_DIR}/json/reader.h
        ${JSONCPP_INCLUDE_DIR}/json/writer.h
        ${JSONCPP_INCLUDE_DIR}/json/assertions.h
        ${JSONCPP_INCLUDE_DIR}/json/version.h
        )

SET(jsoncpp_sources
        ${JSONCPP_SRC_DIR}/json_tool.h
        ${JSONCPP_SRC_DIR}/json_reader.cpp
        ${JSONCPP_SRC_DIR}/json_valueiterator.inl
        ${JSONCPP_SRC_DIR}/json_value.cpp
        ${JSONCPP_SRC_DIR}/json_writer.cpp
        ${JSONCPP_SRC_DIR}/version.h.in)

ADD_LIBRARY(jsoncpp_lib SHARED ${PUBLIC_HEADERS} ${jsoncpp_sources})


include_directories(${ONBOARDSDK_SOURCE}/api/inc)
include_directories(${ONBOARDSDK_SOURCE}/utility/inc)
include_directories(${ONBOARDSDK_SOURCE}/hal/inc)
include_directories(${ONBOARDSDK_SOURCE}/protocol/inc)
include_directories(${ONBOARDSDK_SOURCE}/platform/linux/inc)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)
FILE(GLOB SOURCE_FILES *.hpp *.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/dji_linux_environment.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/dji_linux_helpers.cpp
        )

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} djiosdk-core SimpleAmqpClient jsoncpp_lib ${Rabbitmqc_LIBRARY} ${Boost_LIBRARIES} ${SOCKET_LIBRARY})


