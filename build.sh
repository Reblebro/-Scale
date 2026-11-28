#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
output=$(mktemp .scale-frontier.XXXXXX)
trap 'rm -f -- "$output"' EXIT HUP INT TERM
c++ -std=c++20 -pthread -O2 -Wall -Wextra -Wpedantic \
    src/main.cpp src/contact-rig.cpp src/world-picker.cpp src/world.cpp src/shovel.cpp src/render.cpp src/game.cpp \
    src/micro.cpp src/expedition.cpp src/game-expedition.cpp src/terrain-tests.cpp src/atmosphere.cpp src/settlement.cpp src/game-settlement.cpp \
    -Wl,-l:libSDL2-2.0.so.0 -o "$output"
chmod 755 "$output"
mv -f -- "$output" scale-frontier
