add_executable(smw WIN32
	main.cpp

	ai.cpp
	AINav.cpp
	CpuDriver.cpp
	MatchHistory.cpp
	GameplayTheater.cpp
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
	GameplayCorrection.cpp
	GameplayInterpolation.cpp
	GameplayInterpolation.h
	GameplayMapObjects.cpp
	GameplayEdit.cpp
	GameplayPause.cpp
	NetOverlay.cpp
	NetOverlay.h
	GameplayScoreboard.cpp
	GameplaySetup.cpp
	GameplayTick.cpp
	GSMenu.cpp
	GSMenu.h
	Determinism.cpp
	Determinism.h
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

# The map and world editors, built in (editors/EditorLauncher.h); not on the web.
# On the Xbox they're worked with the controllers (and a USB keyboard and mouse if any)
if(NOT EMSCRIPTEN)
	target_sources(smw PRIVATE
		editors/CoEdit.cpp
		editors/CoEdit.h
		editors/EditorInput.cpp
		editors/EditorInput.h
		editors/EditorInputDraw.cpp
		editors/EditorInputInternal.h
		editors/EditorLauncher.cpp
		editors/EditorLauncher.h
		editors/EmbeddedLevelEditor.cpp
		editors/EmbeddedWorldEditor.cpp
	)
	target_compile_definitions(smw PRIVATE SMW_HAS_EDITORS)
endif()

#
# Dependencies
#

target_link_libraries(smw CommonFiles)

if(NOT EMSCRIPTEN AND NOT XBOXOG)
	target_link_libraries(smw smw_zlib)
endif()

target_link_libraries(smw toml11::toml11)

#
# Original Xbox: the .xbe, and a disc image with the game data
#

if(XBOXOG)
	target_sources(smw PRIVATE "${SDL3_XBOX_MAIN}")
	# A test disc: the game starts the determinism test (src/smw/Determinism.h) with the arguments
	# of tests/determinism/xbox_args.txt, comparing with the PC's checksums (reference.txt)
	option(SMW_XBOX_DETERMINISM_TEST "Xbox: a disc that runs the determinism test instead of the game" OFF)
	set(smw_xbox_deploy "data=${PROJECT_SOURCE_DIR}/data")
	if(SMW_XBOX_DETERMINISM_TEST)
		list(APPEND smw_xbox_deploy
			"smw_args.txt=${PROJECT_SOURCE_DIR}/tests/determinism/xbox_args.txt"
			"determinism=${PROJECT_SOURCE_DIR}/tests/determinism")
	endif()
	# A test disc: the game run with these arguments (an auto-play run, e.g. tests/xbox/memory_*.txt;
	# it shows the memory report at its end)
	set(SMW_XBOX_ARGS_FILE "" CACHE FILEPATH "Xbox: a disc that runs the game with these arguments (smw_args.txt on the disc)")
	if(SMW_XBOX_ARGS_FILE)
		list(APPEND smw_xbox_deploy "smw_args.txt=${SMW_XBOX_ARGS_FILE}" "xboxtest=${PROJECT_SOURCE_DIR}/tests/xbox")
	endif()
	rxdk_add_xbe(smw
		TITLE_NAME "Super Mario War"
		TITLE_ID 0x534D0001
		STACK 1048576
		ISO
		ISO_NAME supermariowar
		DEPLOY ${smw_xbox_deploy}
	)
endif()

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
