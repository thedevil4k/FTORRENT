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
#   file_hash       the SHA-256 vectors and the VirusTotal URL validation
#   archive_inspector
#                   read-only listing and hashing inside a generated ZIP, the
#                   RAR5/ZIP fixtures and the refusals for bad input (only when
#                   libarchive is found)
#   security_analyzer
#                   the metadata heuristics: file families, hostile names,
#                   symlinks, torrent-level counts and the non-verdict wording
#   security_badge  risk labels/palette, name sanitising and the rendered shield
#   security_keys   the key store: round trip, removal and the stored bytes
#   reputation      provider parsing, the service's states and wording, and the
#                   exact no-hash sentence (fake providers, no network)
#   archive_preflight
#                   piece arithmetic and a real ZIP/RAR5 listing rebuilt from
#                   only its first and last piece
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
ftorrent_add_test(file_hash file_hash.cpp)
ftorrent_add_test(security_analyzer security_analyzer.cpp)
ftorrent_add_test(security_badge security_badge.cpp)
ftorrent_add_test(security_keys security_keys.cpp)
ftorrent_add_test(reputation reputation.cpp)
ftorrent_add_test(archive_preflight archive_preflight.cpp)

# The archive reader is optional, so its test is only built when the reader is.
# FTORRENT_HAVE_LIBARCHIVE is set by the parent CMakeLists.txt and also reaches
# this test through ftorrent_core's PUBLIC compile definition.
if(FTORRENT_HAVE_LIBARCHIVE)
    ftorrent_add_test(archive_inspector archive_inspector.cpp)
    # Real RAR5 and hostile-name ZIP fixtures. The path is compiled in so the
    # test can run from any working directory, including a multi-config IDE.
    target_compile_definitions(archive_inspector PRIVATE
        FTORRENT_ARCHIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/archives")
endif()

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
add_test(NAME file_hash COMMAND file_hash)
add_test(NAME security_analyzer COMMAND security_analyzer)
add_test(NAME security_badge COMMAND security_badge)
add_test(NAME security_keys COMMAND security_keys)
add_test(NAME reputation COMMAND reputation)
add_test(NAME archive_preflight COMMAND archive_preflight)

set(FTORRENT_TESTS toolbar_layout toolbar_clicks icons_full flows file_hash
    security_analyzer security_badge security_keys reputation archive_preflight)
if(FTORRENT_HAVE_LIBARCHIVE)
    add_test(NAME archive_inspector COMMAND archive_inspector)
    list(APPEND FTORRENT_TESTS archive_inspector)
endif()

foreach(t ${FTORRENT_TESTS})
    set_tests_properties(${t} PROPERTIES
        ENVIRONMENT "${FTORRENT_TEST_ENV}"
        # A test that opens a session and talks to a tracker is slower than a
        # geometry sweep; the default 1500s would hide a hang rather than report.
        TIMEOUT 600
    )
endforeach()
set_tests_properties(toolbar_layout PROPERTIES TIMEOUT 300)

# Labels let CI choose a subset without naming tests one by one: `unit` is
# headless and offline, `ui` needs FLTK but not the network, and `network`
# covers the end-to-end journeys that talk to search engines and trackers.
set_tests_properties(toolbar_layout toolbar_clicks icons_full security_badge
    PROPERTIES LABELS "ui")
set_tests_properties(file_hash security_analyzer security_keys
    PROPERTIES LABELS "unit")
# reputation drives Fl::awake() delivery, so it needs the FLTK event loop even
# though nothing is drawn and no network is touched.
set_tests_properties(reputation PROPERTIES LABELS "ui")
set_tests_properties(archive_preflight PROPERTIES LABELS "unit")
if(FTORRENT_HAVE_LIBARCHIVE)
    target_compile_definitions(archive_preflight PRIVATE
        FTORRENT_ARCHIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/archives")
endif()
set_tests_properties(flows PROPERTIES LABELS "network")
if(FTORRENT_HAVE_LIBARCHIVE)
    set_tests_properties(archive_inspector PROPERTIES LABELS "unit")
endif()

# The assets-missing variant: a copy of the binary in a directory of its own, with
# no assets/ next to it.
set(NOASSETS_DIR ${CMAKE_CURRENT_BINARY_DIR}/noassets)
file(MAKE_DIRECTORY ${NOASSETS_DIR})
add_custom_command(TARGET icons POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:icons> ${NOASSETS_DIR}/icons${CMAKE_EXECUTABLE_SUFFIX}
    COMMENT "Copying icons to a directory with no assets/ for the fallback variant"
)
add_test(NAME icons_missing COMMAND ${NOASSETS_DIR}/icons${CMAKE_EXECUTABLE_SUFFIX} missing)
set_tests_properties(icons_missing PROPERTIES
    ENVIRONMENT "${FTORRENT_TEST_ENV}"
    TIMEOUT 600
    LABELS "ui"
)