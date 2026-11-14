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
	rollback/test_RollbackSession.cpp
)
target_link_libraries(smw-tests PRIVATE doctest CommonFiles SmwRollbackNet)
doctest_discover_tests(smw-tests)
