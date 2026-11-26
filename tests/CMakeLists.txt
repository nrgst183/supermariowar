add_executable(smw-tests
	runner.cpp
	common/test_FileIO.cpp
	common/test_GameModeSettingsSerialization.cpp
	common/test_GameValuesToml.cpp
	common/test_InputFrame.cpp
	common/test_RandomNumberGenerator.cpp
	common/test_RenderInterpolation.cpp
	common/test_SimArena.cpp
	common/test_version.cpp
	common/test_tokenize.cpp
	common/math/test_vec2.cpp
	common/util/test_DirIterator.cpp
	rollback/test_NetLobby.cpp
	rollback/test_NetSeries.cpp
	rollback/test_PortMapping.cpp
	rollback/test_RollbackSession.cpp
)
# The system link discovery is part of the network code
if(NOT NO_NETWORK)
	target_sources(smw-tests PRIVATE rollback/test_LanDiscovery.cpp)
endif()
target_link_libraries(smw-tests PRIVATE doctest CommonFiles SmwRollbackNet)
doctest_discover_tests(smw-tests)

# The determinism test (src/smw/Determinism.h): the game plays a fixed suite of CPU matches twice,
# headless; the second must give the first's state checksums. On Windows (MSVC, where
# tests/determinism/reference.txt was recorded) they must also be the reference's: the checksums the
# Xbox's test disc compares with (-DSMW_XBOX_DETERMINISM_TEST=ON). The dummy video driver: offscreen
# makes its windows OpenGL ones on macOS, through EGL, which macOS doesn't have.
if(TARGET smw)
	set(smw_determinism_args --autotest --at-determinism 2 --at-norender 1 --datadir "${PROJECT_SOURCE_DIR}/data"
		--at-determinism-out "${CMAKE_CURRENT_BINARY_DIR}/determinism_checksums.txt")
	add_test(NAME determinism.repeatable COMMAND smw ${smw_determinism_args})
	set_tests_properties(determinism.repeatable PROPERTIES
		ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SMW_HOME=${CMAKE_CURRENT_BINARY_DIR}/home" TIMEOUT 600 LABELS determinism)
	if(MSVC)
		add_test(NAME determinism.reference COMMAND smw ${smw_determinism_args}
			--at-determinism-ref "${PROJECT_SOURCE_DIR}/tests/determinism/reference.txt")
		set_tests_properties(determinism.reference PROPERTIES
			ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SMW_HOME=${CMAKE_CURRENT_BINARY_DIR}/home" TIMEOUT 600 LABELS determinism)
	endif()
endif()
