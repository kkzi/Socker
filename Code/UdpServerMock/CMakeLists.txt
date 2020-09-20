project(UdpServerMock)


aux_source_directory(Src SRCS)

add_library(UdpServerMock SHARED ${SRCS})