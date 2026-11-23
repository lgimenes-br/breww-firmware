#!/bin/sh
# Gera as fontes Outfit da interface (LVGL 8) em src/fonts/. Uso: ci/gen_fonts.sh <pasta com Outfit_*.ttf> <lv_font_conv>
# Os TTF vem do pacote @expo-google-fonts/outfit (licenca OFL). Tamanhos do prototipo aprovado:
# 3,5" (480x320) em escala 1; 7" (1024x600) em escala 1,875.
set -e
TTF="$1"; CONV="$2"; OUT="$(dirname "$0")/../src/fonts"
FA="$HOME/Documents/Arduino/libraries/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
TEXT="0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x201D,0x2022,0x2026,0x2039-0x203A,0x2212"
DIG="0x20,0x25,0x2C-0x2E,0x30-0x39,0xB0,0x2212"
CODE="0x2D,0x30-0x39,0x41-0x5A"
ICONS="0xF55A,0xF062,0xF00C"
NETICONS="0xF1EB,0xF6FF" # wifi, network-wired (ao lado do relogio)
gen() { # nome tamanho peso faixa guarda [icones]
  name=$1; size=$2; weight=$3; range=$4; guard=$5; icons=$6
  extra=""; [ -n "$icons" ] && extra="--font $FA -r $icons"
  "$CONV" --size "$size" --format lvgl --bpp 4 --no-compress --font "$TTF/Outfit_${weight}.ttf" -r "$range" $extra -o "$OUT/$name.c" --lv-include lvgl.h >/dev/null
  { echo "#include \"../../config.h\""; echo "#if BREWW_HAS_LVGL && $guard"; cat "$OUT/$name.c"; echo "#endif"; } > "$OUT/$name.tmp" && mv "$OUT/$name.tmp" "$OUT/$name.c"
}
S35='!defined(BREWW_PANEL_DSI)'; S7='defined(BREWW_PANEL_DSI)'
for spec in "12 13 15 18 22 28 46|$S35" "22 24 28 34 41 52 86|$S7"; do
  sizes=${spec%%|*}; guard=${spec#*|}; tag=$([ "$guard" = "$S35" ] && echo 35 || echo 7)
  i=0; for s in $sizes; do i=$((i+1)); ic=""; { [ $i = 4 ] || [ $i = 5 ]; } && ic=$ICONS; [ $i = 2 ] && ic=$NETICONS; gen "outfit${tag}_r$s" $s 400Regular "$TEXT" "$guard" "$ic"; done
done
gen outfit35_m15 15 500Medium "$TEXT" "$S35";  gen outfit7_m28 28 500Medium "$TEXT" "$S7"
for s in 24 34 56 88 146; do gen "outfit35_l$s" $s 300Light "$DIG" "$S35"; done
for s in 45 64 105 165 274; do gen "outfit7_l$s" $s 300Light "$DIG" "$S7"; done
gen outfit35_c40 40 300Light "$CODE" "$S35"; gen outfit7_c75 75 300Light "$CODE" "$S7"
