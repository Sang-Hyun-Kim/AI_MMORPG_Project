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

# 빌드 후 Config.json을 실행 파일 위치로 자동 복사
add_custom_command(TARGET GameServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/Config.json"
    "$<TARGET_FILE_DIR:GameServer>"
)
