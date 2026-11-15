target_sources(smw PRIVATE
    MatchConfig.cpp
    Replay.cpp
    Replay.h
    MatchConfig.h
    NetGame.cpp
    NetGame.h
    NetGameSeries.cpp
    SeriesState.cpp
    SeriesState.h
    Snapshot.cpp
    Snapshot.h
    StateChecksum.cpp
    StateChecksum.h
    SyncTest.cpp
    SyncTest.h
)

# The game-independent part of the rollback netplay (session logic,
# protocol, lobby, transport), also linked by the unit tests
add_library(SmwRollbackNet STATIC
    NetConditionSim.cpp
    NetConditionSim.h
    NetLobby.cpp
    NetLobby.h
    NetMesh.cpp
    NetMesh.h
    NetProtocol.h
    NetSeries.cpp
    NetSeries.h
    RollbackSession.cpp
    RollbackSession.h
    SkinTransfer.cpp
    SkinTransfer.h
    Transport.h
)
target_include_directories(SmwRollbackNet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(SmwRollbackNet PUBLIC cxx_std_20)

if(NOT NO_NETWORK)
    target_sources(SmwRollbackNet PRIVATE
        EnetTransport.cpp
        EnetTransport.h
    )
    target_link_libraries(SmwRollbackNet PUBLIC enet)
    if (WIN32)
        target_link_libraries(SmwRollbackNet PUBLIC ws2_32 winmm)
    endif()
endif()

target_link_libraries(smw SmwRollbackNet)
