cmake_minimum_required(VERSION 3.20)

# Set the project name
project(SmartValidator)

set(CMAKE_CXX_STANDARD 17)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

add_subdirectory(./googletest)

set(PROTO_FILES 
    ./protos/matrix.proto 
    ./protos/block.proto 
    ./protos/transaction.proto
    ./protos/components.proto
)

# Generate C++ source and header files from protobuf
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
add_executable(testDAGModule ./dagModule/testDAGModule.cc ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(testDAGModule ${Protobuf_LIBRARIES} gtest gtest_main)
