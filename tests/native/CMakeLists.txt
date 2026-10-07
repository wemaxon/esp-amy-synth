cmake_minimum_required(VERSION 3.24)
project(synth_native_tests LANGUAGES C)
enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT EXISTS "${REPO_ROOT}/components/amy/amy/src/amy_prng.h")
    message(FATAL_ERROR "Required AMY firmware modifications are missing (amy_prng.h). See docs/maintenance.md; no suites are skipped.")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Native tests target Linux. On Windows use Ubuntu WSL2.")
endif()

# Only SDK boundary declarations are substituted. Production and libc headers
# are never shadowed. Each subject is compiled normally, not included in a test.
add_library(test_build_options INTERFACE)
target_include_directories(test_build_options INTERFACE
    "${REPO_ROOT}/main"
    "${CMAKE_CURRENT_SOURCE_DIR}/fakes/esp_idf"
    "${REPO_ROOT}/components/pcf857x"
    "${REPO_ROOT}/components/pcf8574"
    "${REPO_ROOT}/components/pcf8575"
    "${REPO_ROOT}/components/amy/amy/src")
target_compile_options(test_build_options INTERFACE -Wall -Wextra -Werror)
target_compile_definitions(test_build_options INTERFACE BLOCK_SIZE_BITS=8)

function(native_test name)
    add_executable(${name} "${name}_harness.c")
    target_link_libraries(${name} PRIVATE test_build_options)
    if(ARGN)
        set(subject_sources)
        foreach(source IN LISTS ARGN)
            list(APPEND subject_sources "${REPO_ROOT}/${source}")
        endforeach()
        add_library(${name}_subject STATIC ${subject_sources})
        target_link_libraries(${name}_subject PRIVATE test_build_options)
        target_link_libraries(${name} PRIVATE ${name}_subject)
    endif()
    target_link_libraries(${name} PRIVATE m)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

native_test(looper_dsp
    main/synth/looper_dsp.c)
native_test(wheel
    main/synth/app/wheel.c
    main/synth/app/settings.c)
native_test(control_logic
    main/hmi/control_logic.c)
native_test(calibration_math
    main/hmi/calibration_math.c)
native_test(looper_view
    main/synth/looper_view.c)
native_test(menu
    main/synth/app/control_route.c
    main/synth/app/sound.c)
native_test(pitch_detector
    main/synth/pitch_detector.c)
native_test(transport_regression
    main/synth/transport_timing.c
    main/synth/app/control_route.c)
native_test(playback
    main/synth/app/chords.c
    main/synth/app/play_state.c
    main/synth/app/rhythm.c
    main/synth/app/tempo.c
    main/synth/play_mode.c
    main/synth/app/playback.c)
native_test(mic_finalize
    main/synth/app/mic.c)
native_test(amy_prng)
native_test(audio_meter
    main/synth/audio_meter.c
    tests/native/fakes/meter_clock.c)
native_test(looper_track
    main/synth/looper_dsp.c
    main/synth/looper_track.c)
native_test(rhythm
    main/synth/app/rhythm.c)
native_test(amy_event_builder
    main/synth/amy_event_builder.c
    components/amy/amy/src/api.c)
native_test(settings
    main/synth/app/settings.c)
native_test(chord
    main/synth/chord_engine.c
    main/synth/amy_voice_map.c
    main/synth/app/chord_tools.c
    main/synth/app/chords.c
    main/synth/app/key.c)
native_test(calibration_store
    main/hmi/calibration_store.c)
native_test(pcf857x
    components/pcf857x/pcf857x.c
    components/pcf8574/pcf8574.c
    components/pcf8575/pcf8575.c)
native_test(mic_sample_dsp
    main/synth/mic_sample_dsp.c)
native_test(looper_transport
    main/synth/looper_transport.c)
native_test(looper_take
    main/synth/looper_take.c)
native_test(drum_transport
    main/synth/app/drum_transport.c
    main/synth/transport_timing.c)
native_test(beat_scheduler
    main/synth/beat_scheduler.c
    main/synth/transport_timing.c)

# Link the real upstream default-event initialization, replacing only enqueue.
# Section GC omits unrelated upstream API functions and their engine dependencies.
target_compile_options(amy_event_builder_subject PRIVATE -ffunction-sections -fdata-sections)
target_link_options(amy_event_builder PRIVATE -Wl,--gc-sections -Wl,--wrap=amy_add_event)
