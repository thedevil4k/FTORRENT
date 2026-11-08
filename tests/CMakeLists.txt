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
#                   read-only listing inside a generated ZIP, the RAR5/ZIP
#                   fixtures and the refusals for bad input (only when
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
#   archive_preflight_network
#                   the same check against a loopback seeder: a RAR 1.5-4.x
#                   lists its entries from a swarm and the seeder's upload
#                   counter proves the archive was never downloaded
#   security_events the engine event log: order, cap and what it drops
#   quality_gates   not a program: it reads the source tree and checks the
#                   promises that are about its shape and its wording rather
#                   than about behaviour - the size limit, the exact no-hash
#                   sentence, the badge order, the shield on all four
#                   surfaces, the providers asked only from a click
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
function(ftorrent_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE ftorrent_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

ftorrent_add_test(toolbar_layout toolbar_layout.cpp)
ftorrent_add_test(toolbar_clicks toolbar_clicks.cpp)
ftorrent_add_test(add_torrent_dialog add_torrent_dialog.cpp)
ftorrent_add_test(preferences_keys preferences_keys.cpp)
ftorrent_add_test(flows flows.cpp)
ftorrent_add_test(icons icons.cpp icons_geometry.cpp)
ftorrent_add_test(file_hash file_hash.cpp)
ftorrent_add_test(security_analyzer security_analyzer.cpp)
ftorrent_add_test(security_badge security_badge.cpp)
ftorrent_add_test(security_keys security_keys.cpp)
ftorrent_add_test(reputation reputation.cpp)
ftorrent_add_test(archive_preflight archive_preflight.cpp)
ftorrent_add_test(archive_preflight_network archive_preflight_network.cpp)
ftorrent_add_test(security_events security_events.cpp)
ftorrent_add_test(search_results search_results.cpp)
ftorrent_add_test(search_scope search_scope.cpp)
ftorrent_add_test(search_parse search_parse.cpp)
ftorrent_add_test(filelist filelist.cpp)
ftorrent_add_test(filelist_network filelist_network.cpp)
ftorrent_add_test(format format.cpp)

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
foreach(stage icons toolbar_layout toolbar_clicks add_torrent_dialog flows)
    add_dependencies(${stage} ftorrent_test_assets)
endforeach()

add_test(NAME toolbar_layout COMMAND toolbar_layout)
add_test(NAME toolbar_clicks COMMAND toolbar_clicks)
add_test(NAME add_torrent_dialog COMMAND add_torrent_dialog)
add_test(NAME preferences_keys COMMAND preferences_keys)
add_test(NAME icons_full COMMAND icons full)
add_test(NAME flows COMMAND flows)
add_test(NAME file_hash COMMAND file_hash)
add_test(NAME security_analyzer COMMAND security_analyzer)
add_test(NAME security_badge COMMAND security_badge)
add_test(NAME security_keys COMMAND security_keys)
add_test(NAME reputation COMMAND reputation)
add_test(NAME archive_preflight COMMAND archive_preflight)
add_test(NAME archive_preflight_network COMMAND archive_preflight_network)
add_test(NAME security_events COMMAND security_events)
add_test(NAME search_results COMMAND search_results)
add_test(NAME search_scope COMMAND search_scope)
add_test(NAME search_parse COMMAND search_parse)
add_test(NAME filelist COMMAND filelist)
add_test(NAME filelist_network COMMAND filelist_network)
add_test(NAME format COMMAND format)

# quality_gates is not a program: it reads the source tree and checks the
# product invariants that are about its shape and its wording rather than about
# behaviour - a file that outgrew the size limit, a badge drawn in the wrong
# order, a verdict that starts saying "safe", a provider asked from a fourth
# place. The other suites check behaviour; this one checks the promises.
#
# It needs Python, which every machine that builds the project has, but a
# missing interpreter must not break the configure step: the test is simply
# not registered and the checker can still be run by hand.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_EXECUTABLE)
    find_package(PythonInterp QUIET)
endif()
if(Python3_EXECUTABLE OR PYTHON_EXECUTABLE)
    if(Python3_EXECUTABLE)
        set(FTORRENT_PYTHON ${Python3_EXECUTABLE})
    else()
        set(FTORRENT_PYTHON ${PYTHON_EXECUTABLE})
    endif()
    add_test(NAME quality_gates
        COMMAND ${FTORRENT_PYTHON}
                ${CMAKE_SOURCE_DIR}/scripts/quality/check_invariants.py
                --root ${CMAKE_SOURCE_DIR})
    set(FTORRENT_QUALITY_GATES TRUE)
else()
    message(STATUS
        "Python not found: the quality_gates test is not registered. "
        "Run scripts/quality/check_invariants.py by hand instead.")
endif()

set(FTORRENT_TESTS toolbar_layout toolbar_clicks add_torrent_dialog
    preferences_keys icons_full flows file_hash
    security_analyzer security_badge security_keys reputation archive_preflight
    archive_preflight_network security_events search_results search_scope
    search_parse filelist filelist_network format)
if(FTORRENT_QUALITY_GATES)
    list(APPEND FTORRENT_TESTS quality_gates)
endif()
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
set_tests_properties(toolbar_layout toolbar_clicks add_torrent_dialog
    preferences_keys icons_full security_badge search_results
    PROPERTIES LABELS "ui")
set_tests_properties(file_hash security_analyzer security_keys
    PROPERTIES LABELS "unit")
set_tests_properties(search_parse filelist format PROPERTIES LABELS "unit")
# The swarm probe: real trackers, real DHT, real .torrent downloads.
set_tests_properties(filelist_network PROPERTIES LABELS "network")
# The loopback archive check: a real libtorrent session and a real peer, but
# both on this machine, so nothing leaves it.
set_tests_properties(archive_preflight_network PROPERTIES LABELS "network")
# reputation drives Fl::awake() delivery, so it needs the FLTK event loop even
# though nothing is drawn and no network is touched.
set_tests_properties(reputation PROPERTIES LABELS "ui")
set_tests_properties(archive_preflight security_events PROPERTIES LABELS "unit")
if(FTORRENT_QUALITY_GATES)
    set_tests_properties(quality_gates PROPERTIES LABELS "unit")
endif()
if(FTORRENT_HAVE_LIBARCHIVE)
    target_compile_definitions(archive_preflight PRIVATE
        FTORRENT_ARCHIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/archives")
    # The loopback swarm test reads the same fixtures and needs the same path.
    target_compile_definitions(archive_preflight_network PRIVATE
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