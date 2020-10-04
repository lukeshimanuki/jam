CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

project(JAM)

set(HOST "aws.shimanuki.cc" CACHE STRING "The host of the server that the client connects to by default.")
set(PORT "3141" CACHE STRING "The port the server listens on and the client connects to by default.")

add_subdirectory(portaudio EXCLUDE_FROM_ALL)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -DASIO_STANDALONE -std=c++11")

# Compile server.
add_definitions(-DPORT=${PORT})
add_executable(jamserver server.cpp)

# Link -pthread if available.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
if(THREADS_HAVE_PTHREAD_ARG)
  set_property(TARGET jamserver PROPERTY COMPILE_OPTIONS "-pthread")
  set_property(TARGET jamserver PROPERTY INTERFACE_COMPILE_OPTIONS "-pthread")
endif()
if(CMAKE_THREAD_LIBS_INIT)
  target_link_libraries(jamserver "${CMAKE_THREAD_LIBS_INIT}")
endif()

# Include asio.
include_directories(asio/asio/include)

add_definitions(-DHOST=${HOST} -DPORT=${PORT})

# Compile client.
add_executable(jam client.cpp)
target_link_libraries(jam portaudio_static)

