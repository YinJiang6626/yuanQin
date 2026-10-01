cmake_minimum_required(VERSION 3.21)

project(yuanqin VERSION 0.1.0 LANGUAGES CXX)

option(YUANQIN_BUILD_TESTS "Build parser tests" ON)

add_library(yuanqin_core STATIC
    src/core/Score.cpp
    src/core/ScoreParser.cpp
)
target_include_directories(yuanqin_core PUBLIC include)
target_compile_features(yuanqin_core PUBLIC cxx_std_20)

add_library(yuanqin_playback STATIC
    src/playback/GenshinWindowTarget.cpp
    src/playback/MusicPlayer.cpp
    src/playback/SerialKeySender.cpp
    src/playback/VirtualHidKeySender.cpp
    src/playback/Win32KeySender.cpp
    src/playback/WindowsApiKeySender.cpp
)
target_include_directories(yuanqin_playback PUBLIC include PRIVATE shared)
target_link_libraries(yuanqin_playback PUBLIC yuanqin_core user32 setupapi)
target_compile_features(yuanqin_playback PUBLIC cxx_std_20)

add_library(yuanqin_ui_model STATIC
    src/ui/CompositionDocument.cpp
    src/ui/ScoreDocument.cpp
)
target_include_directories(yuanqin_ui_model PUBLIC include)
target_link_libraries(yuanqin_ui_model PUBLIC yuanqin_core)
target_compile_features(yuanqin_ui_model PUBLIC cxx_std_20)

add_library(yuanqin_ui STATIC
    src/ui/ScoreEditor.cpp
)
target_include_directories(yuanqin_ui PUBLIC include)
target_link_libraries(yuanqin_ui PUBLIC yuanqin_ui_model user32 gdi32 imm32)
target_compile_features(yuanqin_ui PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(yuanqin_core PRIVATE /W4 /permissive-)
    target_compile_options(yuanqin_playback PRIVATE /W4 /permissive-)
    target_compile_options(yuanqin_ui_model PRIVATE /W4 /permissive-)
    target_compile_options(yuanqin_ui PRIVATE /W4 /permissive-)
else()
    target_compile_options(yuanqin_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(yuanqin_playback PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(yuanqin_ui_model PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(yuanqin_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(yuanqin_keyboard apps/keyboard_console/main.cpp)
target_link_libraries(yuanqin_keyboard PRIVATE yuanqin_playback)

add_executable(yuanqin_serial apps/serial_console/main.cpp)
target_link_libraries(yuanqin_serial PRIVATE yuanqin_playback)

add_executable(yuanqin_gui WIN32
    apps/gui/main.cpp
    apps/gui/MainWindow.cpp
)
target_compile_definitions(yuanqin_gui PRIVATE UNICODE _UNICODE)
target_link_libraries(yuanqin_gui PRIVATE yuanqin_ui yuanqin_playback comdlg32 msimg32)

if(YUANQIN_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_executable(yuanqin_score_parser_tests tests/ScoreParserTests.cpp)
    target_link_libraries(yuanqin_score_parser_tests PRIVATE yuanqin_core)
    add_test(NAME yuanqin_score_parser_tests COMMAND yuanqin_score_parser_tests)

    add_executable(yuanqin_score_document_tests tests/ScoreDocumentTests.cpp)
    target_link_libraries(yuanqin_score_document_tests PRIVATE yuanqin_ui_model)
    add_test(NAME yuanqin_score_document_tests COMMAND yuanqin_score_document_tests)

    add_executable(yuanqin_composition_document_tests tests/CompositionDocumentTests.cpp)
    target_link_libraries(yuanqin_composition_document_tests PRIVATE yuanqin_ui_model)
    add_test(NAME yuanqin_composition_document_tests COMMAND yuanqin_composition_document_tests)

    add_executable(yuanqin_music_player_tests tests/MusicPlayerTests.cpp)
    target_link_libraries(yuanqin_music_player_tests PRIVATE yuanqin_playback)
    add_test(NAME yuanqin_music_player_tests COMMAND yuanqin_music_player_tests)
endif()
