#!/bin/bash
# Ends RS Tone Host. It also ends by itself a few seconds after the game does.
d=$(cd "$(dirname "$0")" && pwd)
[ -p "$d/commands" ] && echo quit > "$d/commands"
