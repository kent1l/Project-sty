@echo off
echo Setting up MSVC Build Environment...
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

echo Building Arranger Engine with MSVC...
cl /std:c++17 /EHsc /O2 ^
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
    /I"vendor/rtmidi" ^
    /I"src" ^
    /D__WINDOWS_MM__ ^
    winmm.lib ^
    ws2_32.lib ^
    /Fe:ArrangerEngine.exe

if %errorlevel% neq 0 (
    echo MSVC Build Failed!
    exit /b %errorlevel%
)

echo MSVC Build Succeeded! Run ArrangerEngine.exe to start.
