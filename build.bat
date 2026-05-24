@echo off
echo Building Arranger Engine...

g++ -std=c++17 ^
    src/main.cpp ^
    src/reader/SFF2Parser.cpp ^
    src/engine/ChordRecognizer.cpp ^
    src/engine/TranspositionBrain.cpp ^
    src/engine/MegaVoiceTranslator.cpp ^
    src/engine/StyleController.cpp ^
    src/engine/RegistrationMemory.cpp ^
    src/engine/Sequencer.cpp ^
    src/listener/MidiListener.cpp ^
    src/listener/MidiOutput.cpp ^
    src/listener/WebSocketServer.cpp ^
    src/clock/MasterClock.cpp ^
    vendor/rtmidi/RtMidi.cpp ^
    -I"vendor/rtmidi" ^
    -I"src" ^
    -D__WINDOWS_MM__ ^
    -lwinmm ^
    -lws2_32 ^
    -o ArrangerEngine.exe

if %errorlevel% neq 0 (
    echo Build Failed!
    exit /b %errorlevel%
)

echo Build Succeeded! Run ArrangerEngine.exe to start.
