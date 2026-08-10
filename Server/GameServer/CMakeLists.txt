project(GameServer)

add_executable(GameServer 
    GameServer.cpp
    Packet/ServerPacketHandler.cpp
)

target_include_directories(GameServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Packet)

# ServerCore 링크 (CorePch 등 헤더 포함)
target_link_libraries(GameServer PRIVATE ServerCore protobuf::libprotobuf)
