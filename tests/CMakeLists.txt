FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(ses_tests
    ModelTests.cpp
    SerializationTests.cpp
    PluginStateTests.cpp
    TransportTests.cpp
    MultiPointTests.cpp
    EditorTests.cpp
    PitchStutterTests.cpp
    EngineTests.cpp
    FreeTimeShaperTests.cpp
    ShaperLevelProbe.cpp
    EffectsChainTests.cpp)

target_link_libraries(ses_tests PRIVATE
    ses_shared
    Catch2::Catch2WithMain)

# The test binary is a plain console program, not a plugin.
target_compile_definitions(ses_tests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JucePlugin_Name="SpatialEchoStudioTests")


include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(ses_tests)
