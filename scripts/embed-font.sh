#!/bin/sh
# Regenerates font_embed.h from typewriter.ttf.
# Run from the repo root whenever typewriter.ttf changes:  sh scripts/embed-font.sh
set -e
cd "$(dirname "$0")/.."
[ -f typewriter.ttf ] || { echo "typewriter.ttf not found" >&2; exit 1; }
command -v xxd >/dev/null 2>&1 || { echo "xxd not found (install vim/xxd)" >&2; exit 1; }
{
  echo "/* Auto-generated from typewriter.ttf by scripts/embed-font.sh - do not edit. */"
  echo "/* Special Elite by Astigmatic (AOETI), Apache License 2.0. */"
  xxd -i typewriter.ttf \
    | sed -e 's/^unsigned char typewriter_ttf\[\]/static const unsigned char font_embed_data[]/' \
          -e 's/^unsigned int typewriter_ttf_len/static const unsigned int font_embed_len/'
} > font_embed.h
echo "wrote font_embed.h ($(wc -c < font_embed.h) bytes)"
