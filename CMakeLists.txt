CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

project(JAM)

add_subdirectory(portaudio EXCLUDE_FROM_ALL)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -DASIO_STANDALONE -std=c++11")

# Compile server.
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

# Compile client.
add_executable(jam client.cpp)
target_link_libraries(jam portaudio_static)

