add_executable(launcherTests
    TestMain.cpp
    TestBasics.cpp
    TestCatalog.cpp
    TestInstaller.cpp
    TestNetwork.cpp
    TestRunner.cpp
)
target_link_libraries(launcherTests PRIVATE launcherCore)
target_compile_definitions(launcherTests PRIVATE OML_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME core COMMAND launcherTests)
add_test(NAME network COMMAND launcherTests --network)
set_tests_properties(network PROPERTIES LABELS network)

#Renders the UI headless against the fixture indexes and saves it, which catches a UI that
#crashes and leaves a screenshot to look at. The config lists only the fixtures, so it needs
#no network.
set(OML_FIXTURE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/archive/index.json")
if(OML_FIXTURE_ROOT MATCHES "^/")
    set(OML_FIXTURE_URL "file://${OML_FIXTURE_ROOT}")
else()
    set(OML_FIXTURE_URL "file:///${OML_FIXTURE_ROOT}")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/captureData/config.json
    "{\"sources\": [{\"name\": \"fixtures\", \"url\": \"${OML_FIXTURE_URL}\"}], \"titles\": {\"exampleGame\": \"Example Game\"}}\n")
add_test(NAME uiCapture COMMAND OtherMythosLauncher --data-dir ${CMAKE_CURRENT_BINARY_DIR}/captureData --platform linux
    --capture ${CMAKE_CURRENT_BINARY_DIR}/capture.bmp)
set_tests_properties(uiCapture PROPERTIES LABELS ui)
