cmake_minimum_required(VERSION 3.20)

# Set the project name
project(SmartValidator)

set(CMAKE_CXX_STANDARD 17)

# Find required packages
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(GTest REQUIRED)
find_package(absl REQUIRED)

# Add include directories for M3 Mac
include_directories(/opt/homebrew/include)
include_directories(${CMAKE_BINARY_DIR})

# Add library path for M3 Mac
link_directories(/opt/homebrew/lib)

set(PROTO_FILES 
    ./matrix.proto 
    ./block.proto 
    ./transaction.proto
    ./components.proto
)

# Generate C++ source and header files from protobuf
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
add_executable(testDAGModule ./testDAGModule.cc ${PROTO_SRCS} ${PROTO_HDRS})

# Link all required libraries
target_link_libraries(testDAGModule 
    PRIVATE
    ${Protobuf_LIBRARIES}
    gtest 
    gtest_main
    absl::base
    absl::log
    absl::strings
    absl::synchronization
    absl::log_internal_check_op
    absl::log_internal_message
    absl::log_internal_nullguard
    absl::log_internal_proto
    absl::log_severity
    absl::log_sink
    absl::log_sink_registry
)