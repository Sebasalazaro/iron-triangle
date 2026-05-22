#!/bin/bash
# test_roundtrip.sh — verifica que compress → decompress reproduce el input exacto.
# Uso: bash tests/test_roundtrip.sh  (desde la raíz del repo)
set -e

BINARY="./iron-triangle"
PASS=0
FAIL=0

# Colores opcionales
RED='\033[0;31m'
GRN='\033[0;32m'
NC='\033[0m'

# -------------------------------------------------------------------------
# run_test <nombre> <archivo_entrada>
#   Comprime el archivo, descomprime, hace diff. Imprime PASS o FAIL.
# -------------------------------------------------------------------------
run_test() {
    local name="$1"
    local input="$2"
    local compressed="/tmp/it_test_$$.lz77"
    local recovered="/tmp/it_test_$$.out"

    # Comprimir
    "$BINARY" -c "$input" "$compressed" 2>/dev/null

    # Descomprimir
    "$BINARY" -d "$compressed" "$recovered" 2>/dev/null

    # Comparar bit a bit
    if diff -q "$input" "$recovered" > /dev/null 2>&1; then
        local orig_sz comp_sz
        orig_sz=$(wc -c < "$input")
        comp_sz=$(wc -c < "$compressed")
        ratio=$(awk "BEGIN { printf \"%.1f\", $comp_sz * 100 / $orig_sz }")
        printf "${GRN}PASS${NC}  %-30s  %6d → %6d bytes (%s%%)\n" \
               "$name" "$orig_sz" "$comp_sz" "$ratio"
        PASS=$((PASS + 1))
    else
        printf "${RED}FAIL${NC}  %-30s  output differs from input\n" "$name"
        diff "$input" "$recovered" | head -10 || true
        FAIL=$((FAIL + 1))
    fi

    rm -f "$compressed" "$recovered"
}

# -------------------------------------------------------------------------
# Verificar que el binario existe
# -------------------------------------------------------------------------
if [ ! -x "$BINARY" ]; then
    echo "Error: '$BINARY' no encontrado. Ejecuta 'make' primero."
    exit 1
fi

echo "=== iron-triangle — LZ77 Roundtrip Tests ==="
echo ""

# -------------------------------------------------------------------------
# Test 1: texto repetitivo (alta compresibilidad)
# -------------------------------------------------------------------------
T1="/tmp/it_test_repetitive_$$.txt"
python3 -c "
line = 'the quick brown fox jumps over the lazy dog\n'
block = line * 50 + 'abcdef\n' * 20 + '1234567890\n' * 20
print(block * 40, end='')
" > "$T1"
run_test "texto repetitivo (200 KB)" "$T1"
rm -f "$T1"

# -------------------------------------------------------------------------
# Test 2: archivo pequeño (caso borde — pocos bytes)
# -------------------------------------------------------------------------
T2="/tmp/it_test_small_$$.txt"
printf "hola mundo\nesto es una prueba\n" > "$T2"
run_test "archivo pequeño (< 40 B)" "$T2"
rm -f "$T2"

# -------------------------------------------------------------------------
# Test 3: un solo byte
# -------------------------------------------------------------------------
T3="/tmp/it_test_onebyte_$$.txt"
printf "X" > "$T3"
run_test "un solo byte" "$T3"
rm -f "$T3"

# -------------------------------------------------------------------------
# Test 4: texto con código fuente (moderadamente compresible)
# -------------------------------------------------------------------------
T4="/tmp/it_test_code_$$.c"
python3 -c "
import sys
lines = [
    '#include <stdio.h>\n',
    '#include <stdlib.h>\n',
    'int main(int argc, char *argv[]) {\n',
    '    printf(\"hello world\\n\");\n',
    '    return 0;\n',
    '}\n',
]
for _ in range(300):
    for line in lines:
        sys.stdout.write(line)
" > "$T4"
run_test "código fuente repetido (12 KB)" "$T4"
rm -f "$T4"

# -------------------------------------------------------------------------
# Test 5: runs de caracteres (máxima compresibilidad para LZ77)
# -------------------------------------------------------------------------
T5="/tmp/it_test_runs_$$.bin"
python3 -c "
import sys
sys.stdout.buffer.write(b'A' * 4096)
sys.stdout.buffer.write(b'B' * 4096)
sys.stdout.buffer.write(b'AB' * 2048)
sys.stdout.buffer.write(b'\\x00' * 4096)
" > "$T5"
run_test "runs de bytes (16 KB)" "$T5"
rm -f "$T5"

# -------------------------------------------------------------------------
# Resumen
# -------------------------------------------------------------------------
echo ""
echo "Resultados: $PASS passed, $FAIL failed"
echo ""

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
