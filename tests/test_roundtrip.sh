#!/bin/bash
# test_roundtrip.sh — verifica roundtrip LZ77 y pipeline completo LZ77+RC4.
# Uso: bash tests/test_roundtrip.sh  (desde la raíz del repo, después de make)

BINARY="./iron-triangle"
PASS=0
FAIL=0
RED='\033[0;31m'
GRN='\033[0;32m'
NC='\033[0m'

# -------------------------------------------------------------------------
# run_test_lz77 <nombre> <archivo>
# -------------------------------------------------------------------------
run_test_lz77() {
    local name="$1"
    local input="$2"
    local comp="/tmp/it_lz77_$$.tmp"
    local out="/tmp/it_out_$$.tmp"

    # Comprimir — mostrar stderr para ver errores reales
    if ! "$BINARY" -c "$input" "$comp" 2>&1; then
        printf "${RED}FAIL${NC}  [lz77]  %-28s  error en compress\n" "$name"
        FAIL=$((FAIL + 1))
        rm -f "$comp" "$out"
        return
    fi

    # Descomprimir
    if ! "$BINARY" -d "$comp" "$out" 2>&1; then
        printf "${RED}FAIL${NC}  [lz77]  %-28s  error en decompress\n" "$name"
        FAIL=$((FAIL + 1))
        rm -f "$comp" "$out"
        return
    fi

    # Comparar bit a bit
    local orig_sz comp_sz ratio
    orig_sz=$(wc -c < "$input")
    comp_sz=$(wc -c < "$comp")
    ratio=$(awk "BEGIN { printf \"%.1f\", $comp_sz * 100 / $orig_sz }")

    if diff -q "$input" "$out" > /dev/null 2>&1; then
        printf "${GRN}PASS${NC}  [lz77]  %-28s  %6d → %6d B (%s%%)\n" \
               "$name" "$orig_sz" "$comp_sz" "$ratio"
        PASS=$((PASS + 1))
    else
        printf "${RED}FAIL${NC}  [lz77]  %-28s  output difiere del input\n" "$name"
        diff "$input" "$out" | head -5 || true
        FAIL=$((FAIL + 1))
    fi
    rm -f "$comp" "$out"
}

# -------------------------------------------------------------------------
# run_test_pipeline <nombre> <archivo> <clave>
# -------------------------------------------------------------------------
run_test_pipeline() {
    local name="$1"
    local input="$2"
    local key="$3"
    local enc="/tmp/it_enc_$$.tmp"
    local out="/tmp/it_out_$$.tmp"

    if ! IRON_TRIANGLE_KEY="$key" "$BINARY" -e "$input" "$enc" 2>&1; then
        printf "${RED}FAIL${NC}  [pipe]  %-28s  error en encrypt\n" "$name"
        FAIL=$((FAIL + 1))
        rm -f "$enc" "$out"
        return
    fi

    if ! IRON_TRIANGLE_KEY="$key" "$BINARY" -d "$enc" "$out" 2>&1; then
        printf "${RED}FAIL${NC}  [pipe]  %-28s  error en decrypt\n" "$name"
        FAIL=$((FAIL + 1))
        rm -f "$enc" "$out"
        return
    fi

    local orig_sz enc_sz ratio
    orig_sz=$(wc -c < "$input")
    enc_sz=$(wc -c < "$enc")
    ratio=$(awk "BEGIN { printf \"%.1f\", $enc_sz * 100 / $orig_sz }")

    if diff -q "$input" "$out" > /dev/null 2>&1; then
        printf "${GRN}PASS${NC}  [pipe]  %-28s  %6d → %6d B (%s%%)\n" \
               "$name" "$orig_sz" "$enc_sz" "$ratio"
        PASS=$((PASS + 1))
    else
        printf "${RED}FAIL${NC}  [pipe]  %-28s  output difiere del input\n" "$name"
        FAIL=$((FAIL + 1))
    fi
    rm -f "$enc" "$out"
}

# -------------------------------------------------------------------------
# run_test_wrong_key <nombre> <archivo>
# -------------------------------------------------------------------------
run_test_wrong_key() {
    local name="$1"
    local input="$2"
    local enc="/tmp/it_enc_$$.tmp"
    local out="/tmp/it_out_$$.tmp"

    IRON_TRIANGLE_KEY="clave_correcta"  "$BINARY" -e "$input" "$enc" 2>/dev/null
    IRON_TRIANGLE_KEY="clave_INCORRECTA" "$BINARY" -d "$enc"   "$out" 2>/dev/null || true

    if ! diff -q "$input" "$out" > /dev/null 2>&1; then
        printf "${GRN}PASS${NC}  [sec]   %-28s  clave incorrecta → output diferente\n" "$name"
        PASS=$((PASS + 1))
    else
        printf "${RED}FAIL${NC}  [sec]   %-28s  clave incorrecta devolvió el original\n" "$name"
        FAIL=$((FAIL + 1))
    fi
    rm -f "$enc" "$out"
}

# =========================================================================
if [ ! -x "$BINARY" ]; then
    echo "Error: '$BINARY' no encontrado. Ejecuta 'make' primero."
    exit 1
fi

echo "=== iron-triangle — Roundtrip Tests ==="
echo ""

# --- Generar archivos de prueba ---
T_REPET="/tmp/it_repet_$$.txt"
python3 -c "
import sys
line = 'the quick brown fox jumps over the lazy dog\n'
block = (line * 50 + 'abcdef\n' * 20)
data  = (block * 40).encode()
sys.stdout.buffer.write(data)
" > "$T_REPET"

T_SMALL="/tmp/it_small_$$.txt"
printf 'hola mundo\nesto es una prueba de roundtrip\n' > "$T_SMALL"

T_ONEBYTE="/tmp/it_one_$$.txt"
printf 'X' > "$T_ONEBYTE"

T_CODE="/tmp/it_code_$$.c"
python3 -c "
import sys
lines = ['#include <stdio.h>\n','int main(void) {\n','    return 0;\n','}\n']
data  = (''.join(lines) * 400).encode()
sys.stdout.buffer.write(data)
" > "$T_CODE"

T_RUNS="/tmp/it_runs_$$.bin"
python3 -c "
import sys
sys.stdout.buffer.write(b'A'*4096 + b'B'*4096 + b'AB'*2048 + b'\x00'*4096)
" > "$T_RUNS"

# =========================================================================
echo "--- LZ77 (solo compresión) ---"
run_test_lz77 "texto repetitivo (~200 KB)" "$T_REPET"
run_test_lz77 "archivo pequeño"            "$T_SMALL"
run_test_lz77 "un solo byte"               "$T_ONEBYTE"
run_test_lz77 "código fuente (~12 KB)"     "$T_CODE"
run_test_lz77 "runs de bytes (16 KB)"      "$T_RUNS"

echo ""
echo "--- Pipeline completo (LZ77 + RC4) ---"
run_test_pipeline "texto repetitivo"  "$T_REPET"   "mi_clave_secreta"
run_test_pipeline "archivo pequeño"   "$T_SMALL"   "otra_clave_123"
run_test_pipeline "un solo byte"      "$T_ONEBYTE" "x"
run_test_pipeline "runs de bytes"     "$T_RUNS"    "clave con espacios!"

echo ""
echo "--- Seguridad: clave incorrecta ---"
run_test_wrong_key "clave incorrecta" "$T_SMALL"

# =========================================================================
rm -f "$T_REPET" "$T_SMALL" "$T_ONEBYTE" "$T_CODE" "$T_RUNS"

echo ""
echo "Resultados: $PASS passed, $FAIL failed"
echo ""

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
