# The FTorrent test programs.
#
# Each one links ftorrent_core, so it exercises the same objects the app is built
# from: there is no second copy of the code here that could drift away from the
# shipping binary.
#
# The suites are:
#   toolbar_layout  every window width from 400 to 1920, against the policy's own
#                   arithmetic, plus the level thresholds at each boundary
#   toolbar_clicks  every toolbar button clicked at 720px, the narrowest width
#                   size_range() allows
#   icons           the toolbar icons and the level/width/padding/centring rules,
#                   in both the assets-present and assets-missing variants
#   flows           the user journeys end to end; needs the network
#
# Sources are listed by hand rather than globbed, so adding a file is a decision
# someone makes rather than something that happens.

# FLTK opens a window per test, so on a Wayland-only session it needs to be told
# there is no compositor to reach. Kept in one place so every test is run the
# same way, and so it can be overridden where a display genuinely exists.
#
# The fake WAYLAND_DISPLAY is what keeps FLTK on the X11 path, but the X display
# itself is never invented: hardcoding DISPLAY=:0 pointed the tests at a server
# that only exists on a desktop, and left the CI run -- where ctest is wrapped in
# xvfb-run and its server lands on :99 -- with no display at all. So the display
# is forwarded when this configure step can see one, and otherwise left to the
# test process to inherit, which is where xvfb-run's DISPLAY comes from.
if(UNIX AND NOT APPLE AND NOT DEFINED ENV{WAYLAND_DISPLAY})
    set(FTORRENT_TEST_ENV
        "XDG_RUNTIME_DIR=/tmp/ftorrent-test-runtime"
        "WAYLAND_DISPLAY=wayland-fortorrent-tests")
    if(DEFINED ENV{DISPLAY} AND NOT "$ENV{DISPLAY}" STREQUAL "")
        list(APPEND FTORRENT_TEST_ENV "DISPLAY=$ENV{DISPLAY}")
    endif()
else()
    set(FTORRENT_TEST_ENV "")
endif()

# icons is run twice: once with the real assets/ directory beside the binary, and
# once from a directory that has none, so the compiled-in fallback glyphs are the
# ones that get exercised. AssetLoader resolves assets/ from the running
# executable (its own /proc/self/exe on Linux), which is why the second variant is
# a copy of the binary sitting somewhere else rather than a flag.
function(ftorrent_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ftorrent_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

ftorrent_add_test(toolbar_layout toolbar_layout.cpp)
ftorrent_add_test(toolbar_clicks toolbar_clicks.cpp)
ftorrent_add_test(flows flows.cpp)
ftorrent_add_test(icons icons.cpp)

# The assets-present variants need src/assets/ beside the binary, because
# AssetLoader resolves that directory from the running executable rather than
# from any path the app was given. The old out-of-tree harness kept a copy there
# by hand; here the build stages it, so a fresh checkout needs no setup step.
# icons_full asserts those PNGs are the ones on the buttons, so without this it
# would fail on a clean tree and pass only on a machine that had been set up.
#
# One target rather than a command per test, because they all copy into the SAME
# directory and `cmake -E copy_directory` recreates that directory while it
# copies: under -j the copies deleted each other's half-written tree and the
# build died with "No such file or directory". One command, one destination.
add_custom_target(ftorrent_test_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/assets ${CMAKE_CURRENT_BINARY_DIR}/assets
    COMMENT "Staging src/assets beside the test binaries for AssetLoader"
)
foreach(stage icons toolbar_layout toolbar_clicks flows)
    add_dependencies(${stage} ftorrent_test_assets)
endforeach()

add_test(NAME toolbar_layout COMMAND toolbar_layout)
add_test(NAME toolbar_clicks COMMAND toolbar_clicks)
add_test(NAME icons_full COMMAND icons full)
add_test(NAME flows COMMAND flows)

foreach(t toolbar_layout toolbar_clicks icons_full flows)
    set_tests_properties(${t} PROPERTIES
        ENVIRONMENT "${FTORRENT_TEST_ENV}"
        # A test that opens a session and talks to a tracker is slower than a
        # geometry sweep; the default 1500s would hide a hang rather than report.
        TIMEOUT 600
    )
endforeach()
set_tests_properties(toolbar_layout PROPERTIES TIMEOUT 300)
set_tests_properties(flows PROPERTIES LABELS "network")

# The assets-missing variant: a copy of the binary in a directory of its own, with
# no assets/ next to it.
set(NOASSETS_DIR ${CMAKE_CURRENT_BINARY_DIR}/noassets)
file(MAKE_DIRECTORY ${NOASSETS_DIR})
add_custom_command(TARGET icons POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:icons> ${NOASSETS_DIR}/icons
    COMMENT "Copying icons to a directory with no assets/ for the fallback variant"
)
add_test(NAME icons_missing COMMAND ${NOASSETS_DIR}/icons missing)
set_tests_properties(icons_missing PROPERTIES
    ENVIRONMENT "${FTORRENT_TEST_ENV}"
    TIMEOUT 600
)