#!/bin/bash
echo "Starting MIDI device"
fluidsynth -l -s -i -aalsa -o audio.alsa.device=default /usr/share/soundfonts/default.sf2 &
fspid=$!
trap "kill ${fspid}" EXIT

echo "Building and testing Linux build"
g++ -Wall -Wextra -pedantic -fsanitize=address,undefined midines.cpp example.cpp -lasound -o example
./example

echo "Building and testing Windows build"
x86_64-w64-mingw32-g++ -Wall -Wextra -pedantic midines.cpp example.cpp -lwinmm -static -o example.exe
wine ./example.exe
