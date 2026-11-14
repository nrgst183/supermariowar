add_executable(smw WIN32
	main.cpp

	ai.cpp
	AIObjects.cpp
	ai.h
	FPSLimiter.cpp
	FPSLimiter.h
	GSGameplay.cpp
	GSGameplay.h
	GSGameplayInternal.h
	GameplayCollisions.cpp
	GameplayDebug.cpp
	GameplayDraw.cpp
	GameplayEffects.cpp
	GameplayInterpolation.cpp
	GameplayInterpolation.h
	GameplayMapObjects.cpp
	GameplayPause.cpp
	NetOverlay.cpp
	NetOverlay.h
	GameplayScoreboard.cpp
	GameplaySetup.cpp
	GameplayTick.cpp
	GSMenu.cpp
	GSMenu.h
	MenuAutoTest.cpp
	MenuOnline.cpp
	MenuOnlineSeries.cpp
	MenuScript.cpp
	MenuStartGame.cpp
	GSSplashScreen.cpp
	GSSplashScreen.h
	ObjectContainer.cpp
	ObjectContainer.h
	objectgame.cpp
	objectgame.h
	objecthazard.cpp
	objecthazard.h
	player.cpp
	PlayerDraw.cpp
	PlayerKills.cpp
	PlayerMapCollision.cpp
	PlayerMovement.cpp
	PlayerPowerups.cpp
	player.h
	SimulationState.h
	world.cpp
	world.h

	../common/movingplatform.cpp
	../common/movingplatform.h
)

add_subdirectory(player_components)
add_subdirectory(world)
add_subdirectory(menu)
add_subdirectory(gamemodes)
add_subdirectory(objects)
add_subdirectory(rollback)
add_subdirectory(ui)

target_compile_features(smw PUBLIC cxx_std_14)

# NO_NETWORK builds (Emscripten, Android) leave out the rollback netplay's
# ENet parts (see rollback/CMakeLists.txt); NetGame.cpp checks this define
if(NO_NETWORK)
	target_compile_definitions(smw PRIVATE NETWORK_DISABLED)
endif()

if (WIN32)
	# Add EXE icon on Windows
	target_sources(smw PRIVATE "${PROJECT_SOURCE_DIR}/resources/windows/smw.rc")
endif()

#
# Dependencies
#

target_link_libraries(smw CommonFiles)

if(NOT EMSCRIPTEN)
	target_link_libraries(smw smw_zlib)
endif()

target_link_libraries(smw toml11::toml11)

#
# Installation
#

install(TARGETS smw DESTINATION "${SMW_BINDIR}")

# install Linux desktop item and icons
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux" AND NOT SMW_INSTALL_PORTABLE)
	configure_file("${PROJECT_SOURCE_DIR}/resources/linux/supermariowar.desktop.in"
		"${PROJECT_BINARY_DIR}/supermariowar.desktop" @ONLY)

	install(FILES "${PROJECT_BINARY_DIR}/supermariowar.desktop" DESTINATION "${SMW_DESKTOPDIR}")
	install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/smw.xpm" DESTINATION "${SMW_PIXMAPDIR}")
	install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/smw.png" DESTINATION "${SMW_ICONDIR}/32x32/apps/")
endif()
