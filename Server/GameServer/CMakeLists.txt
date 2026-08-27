project(GameServer)

add_executable(GameServer 
    GameServer.cpp
    GameSession.cpp
    Packet/ServerPacketHandler.cpp
    Game/Object/GameObject.cpp
    Game/Object/Player.cpp
    Game/Room/GameRoom.cpp
    Game/Room/GameRoomManager.cpp
)

target_include_directories(GameServer PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/Packet
    ${CMAKE_CURRENT_SOURCE_DIR}/Game/Object
    ${CMAKE_CURRENT_SOURCE_DIR}/Game/Room
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ServerCore 링크 (CorePch 등 헤더 포함)
target_link_libraries(GameServer PRIVATE ServerCore protobuf::libprotobuf)
