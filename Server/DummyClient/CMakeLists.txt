project(DummyClient)

add_executable(DummyClient
    DummyClient.cpp
    Packet/ClientPacketHandler.cpp
)

target_link_libraries(DummyClient PRIVATE ServerCore protobuf::libprotobuf)
target_include_directories(DummyClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Packet)
