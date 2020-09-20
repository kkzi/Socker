project(Socker)
cmake_minimum_required(VERSION 3.17)
set(CMAKE_CXX_STANDARD 20)


find_package(Boost REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(Depends/Asula/Include Depends/Logger/Include)
include_directories(Code)

add_subdirectory(Depends)
add_subdirectory(Code/TcpServerMock)
add_subdirectory(Code/UdpServerMock)