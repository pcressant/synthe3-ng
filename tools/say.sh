#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Interactive debug: each stdin line -> text layer + API (the NVDA path) -> pw-play.
# No NVDA, no Speech Dispatcher, no SSML, punctuation mode 0 (as NVDA).
# Usage: tools/say.sh [RATE 0..15] [PITCH 0..15] [RANGE -100..100] [VOLUME -100..100]
#        echo "texte" | tools/say.sh      or interactive prompt
# Defaults match NVDA defaults (rate 50, pitch 50, inflection 0, volume 100).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
render=$root/build/render_api
[ -x "$render" ] || { echo "Construire d'abord : make build/render_api" >&2; exit 2; }
rate=${1:-4} pitch=${2:-5} range=${3:-0} volume=${4:-0}
work=$(mktemp -d) && trap 'rm -rf "$work"' EXIT
[ -t 0 ] && echo "rate=$rate pitch=$pitch range=$range volume=$volume — Ctrl-D pour quitter"
while IFS= read -r -e -p "> " line; do
    [ -n "$line" ] || continue
    [ -t 0 ] && history -s -- "$line"
    printf '%s' "$line" > "$work/in.txt"
    "$render" "$work/in.txt" "$work/out.pcm" "$rate" "$pitch" "$range" "$volume" >/dev/null &&
        pw-play --raw --format s16 --rate 22050 --channels 1 "$work/out.pcm"
done
