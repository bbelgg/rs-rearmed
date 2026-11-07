#!/bin/bash
# Ends RS Tone Host. It also ends by itself a few seconds after the game does.
d=$(cd "$(dirname "$0")" && pwd)
# Only with a host there: with nobody reading the FIFO a write would wait for ever
pgrep -f "RS Tone Host.app" > /dev/null && echo quit > "$d/commands"
