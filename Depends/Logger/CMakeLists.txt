project(Logger)

add_definitions(-DLOGGER_EXPORTS)

include_directories(Include)
aux_source_directory(Src SRC_LIST)
add_library(Logger SHARED ${SRC_LIST})

