project(TcpServerMock)


aux_source_directory(Src SRCS)

add_library(TcpServerMock SHARED ${SRCS})