#!/bin/sh
# SPDX-License-Identifier: ISC
# Speaks one word of the game into voice/WORD.wav: a Piper voice pitched
# down, mixed with its vocoded copy, with a faint echo.
#
#     tools/voice.sh MODEL WORD
set -e

model=$1
word=$2
raw="-t raw -r 22050 -e signed -b 16 -c 1"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# "sector-one" is said as "Sector, one."
text=$(echo "$word" | tr - ' ' | sed 's/^sector /sector, /')

piper -m "$model" --length-scale 1.35 -f "$tmp/said.wav" -- "$text." 2>/dev/null
sox "$tmp/said.wav" "$tmp/deep.wav" rate 22050 \
	silence 1 0.01 0.1% reverse silence 1 0.01 0.1% reverse \
	pad 0.02 0.5 pitch -300 norm -3
sox "$tmp/deep.wav" $raw - | "$(dirname "$0")/vocoder" 22050 75 0.15 |
	sox $raw - "$tmp/robot.wav" norm -3
sox -m -v 0.6 "$tmp/deep.wav" -v 0.8 "$tmp/robot.wav" -b 16 "voice/$word.wav" \
	echo 1.0 0.95 190 0.16 380 0.06 \
	reverse silence 1 0.01 0.1% reverse norm -1 2>/dev/null
