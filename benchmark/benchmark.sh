#!/bin/bash
# benchmark.sh — mide el tradeoff I/O vs CPU del pipeline iron-triangle.
#
# Escenarios:
#   A) cp clásico             — línea base sin transformación
#   B) Solo LZ77 (-c / -d)    — solo compresión
#   C) LZ77 + RC4  (-e / -d)  — pipeline completo
#
# Métricas:
#   - Tamaño de archivo (stat)
#   - Wall-clock / CPU user / CPU system  (/usr/bin/time -v)
#   - Conteo de syscalls  (strace -c)
#
# Uso: bash benchmark/benchmark.sh  (desde la raíz del repo, tras make)
set -e

# =========================================================================
# Configuración
# =========================================================================
BINARY="./iron-triangle"
BENCH_DIR="benchmark"
RESULTS="$BENCH_DIR/results.txt"
INPUT="$BENCH_DIR/input_50mb.txt"
KEY="benchmark_key_iron_triangle_2024"

OUT_CP="$BENCH_DIR/out_cp.txt"
OUT_LZ77="$BENCH_DIR/out.lz77"
OUT_LZ77_DEC="$BENCH_DIR/out_lz77_dec.txt"
OUT_ENC="$BENCH_DIR/out.itec"
OUT_ENC_DEC="$BENCH_DIR/out_enc_dec.txt"

RED='\033[0;31m'
GRN='\033[0;32m'
BLU='\033[0;34m'
NC='\033[0m'

# =========================================================================
# Verificar dependencias
# =========================================================================
check_deps() {
    local missing=0
    for cmd in python3 strace bc awk stat; do
        if ! command -v "$cmd" &>/dev/null; then
            echo "Error: falta '$cmd'. Instala con: apt install $cmd"
            missing=1
        fi
    done
    if [ ! -x /usr/bin/time ]; then
        echo "Error: /usr/bin/time no encontrado. Instala con: apt install time"
        missing=1
    fi
    if [ ! -x "$BINARY" ]; then
        echo "Error: '$BINARY' no encontrado. Ejecuta 'make' primero."
        missing=1
    fi
    [ "$missing" -eq 0 ] || exit 1
}

# =========================================================================
# Generar archivo de prueba de 50 MB (texto repetitivo, compresible)
# =========================================================================
generate_input() {
    echo -e "${BLU}[gen]${NC} Generando archivo de prueba de 50 MB..."

    # Usamos python3, NO /dev/urandom:
    # Los datos aleatorios tienen entropía máxima → no comprimen.
    # Necesitamos datos con patrones (palabras, estructuras) para que
    # LZ77 encuentre matches y el ratio sea interesante.
    python3 -c "
import sys

# Bloques de texto variado pero repetitivo: palabras, números, código
lines = [
    'the quick brown fox jumps over the lazy dog\n',
    'lorem ipsum dolor sit amet consectetur adipiscing elit\n',
    'int main(int argc, char *argv[]) { return 0; }\n',
    '#include <stdio.h>\n',
    'Sistemas Operativos EAFIT semestre 11 buffer pagina 4096\n',
    '0123456789 abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ\n',
]

target = 50 * 1024 * 1024  # 50 MB exactos
block  = ''.join(lines * 50).encode()  # ~3 KB por bloque
total  = 0
while total < target:
    chunk = block[:target - total]
    sys.stdout.buffer.write(chunk)
    total += len(chunk)
" > "$INPUT"

    local sz
    sz=$(du -h "$INPUT" | cut -f1)
    echo -e "${GRN}     → $INPUT ($sz)${NC}"
}

# =========================================================================
# Capturar métricas de /usr/bin/time -v
# =========================================================================
# Devuelve: wall_ms user_ms sys_ms
parse_time() {
    local time_file="$1"
    local wall user_t sys_t

    # "Elapsed (wall clock) time": puede venir como m:ss.cc o h:mm:ss.cc
    wall_raw=$(grep "Elapsed (wall" "$time_file" | awk -F': ' '{print $2}' | tr -d ' ')
    # Convertir m:ss.cc → segundos
    if echo "$wall_raw" | grep -q ':'; then
        m=$(echo "$wall_raw" | cut -d: -f1)
        s=$(echo "$wall_raw" | cut -d: -f2)
        wall=$(echo "scale=3; $m * 60 + $s" | bc)
    else
        wall="$wall_raw"
    fi

    user_t=$(grep "User time"   "$time_file" | awk '{print $NF}')
    sys_t=$(grep  "System time" "$time_file" | awk '{print $NF}')

    # Convertir a ms
    wall_ms=$(echo "scale=1; $wall  * 1000" | bc)
    user_ms=$(echo "scale=1; $user_t * 1000" | bc)
    sys_ms=$(echo  "scale=1; $sys_t  * 1000" | bc)

    echo "$wall_ms $user_ms $sys_ms"
}

# =========================================================================
# Contar syscalls con strace -c
# =========================================================================
# strace -c agrega overhead (ptrace); se corre separado de las mediciones
# de tiempo para no contaminar los resultados.
count_syscalls() {
    local tmp_strace="/tmp/strace_bench_$$.txt"
    strace -c -o "$tmp_strace" "$@" 2>/dev/null || true
    # Línea "total" al final del resumen
    total=$(grep -E "^[[:space:]]*[0-9]" "$tmp_strace" | awk '{sum += $4} END {print sum}')
    writes=$(grep -E "[[:space:]]write$" "$tmp_strace" | awk '{print $4}')
    reads=$(grep  -E "[[:space:]]read$"  "$tmp_strace" | awk '{print $4}')
    rm -f "$tmp_strace"
    echo "${total:-0} ${writes:-0} ${reads:-0}"
}

# =========================================================================
# Ejecutar escenario con /usr/bin/time -v
# =========================================================================
run_timed() {
    local tmp_time="/tmp/time_bench_$$.txt"
    /usr/bin/time -v "$@" 2>"$tmp_time"
    parse_time "$tmp_time"
    rm -f "$tmp_time"
}

# =========================================================================
# Tamaño de archivo en bytes
# =========================================================================
filesize() {
    stat -c "%s" "$1" 2>/dev/null || echo "0"
}

human_size() {
    local bytes="$1"
    awk "BEGIN { printf \"%.1f MB\", $bytes / 1048576 }"
}

# =========================================================================
# MAIN
# =========================================================================
check_deps

printf "\n${BLU}=== iron-triangle Benchmark ===${NC}\n"
printf "Fecha: %s\n" "$(date '+%Y-%m-%d %H:%M:%S')"

generate_input

INPUT_SIZE=$(filesize "$INPUT")
printf "\nArchivo de prueba: %s bytes (%s)\n\n" "$INPUT_SIZE" "$(human_size "$INPUT_SIZE")"

# ----- Escenario A: cp clásico -----
echo -e "${BLU}[A]${NC} cp clásico..."
read -r A_WALL A_USER A_SYS <<< "$(run_timed cp "$INPUT" "$OUT_CP")"
read -r A_SC A_WR A_RD <<< "$(count_syscalls cp "$INPUT" "$OUT_CP")"
A_OUT_SIZE=$(filesize "$OUT_CP")

# ----- Escenario B: Solo LZ77 (compress + decompress) -----
echo -e "${BLU}[B]${NC} Solo LZ77 (compress)..."
read -r BC_WALL BC_USER BC_SYS <<< "$(run_timed "$BINARY" -c "$INPUT" "$OUT_LZ77")"
read -r BC_SC BC_WR BC_RD <<< "$(count_syscalls "$BINARY" -c "$INPUT" "$OUT_LZ77")"
B_COMP_SIZE=$(filesize "$OUT_LZ77")

echo -e "${BLU}[B]${NC} Solo LZ77 (decompress)..."
read -r BD_WALL BD_USER BD_SYS <<< "$(run_timed "$BINARY" -d "$OUT_LZ77" "$OUT_LZ77_DEC")"
read -r BD_SC BD_WR BD_RD <<< "$(count_syscalls "$BINARY" -d "$OUT_LZ77" "$OUT_LZ77_DEC")"

# Tiempo total B = compress + decompress
B_WALL=$(echo "scale=1; $BC_WALL + $BD_WALL" | bc)
B_USER=$(echo "scale=1; $BC_USER + $BD_USER" | bc)
B_SYS=$(echo  "scale=1; $BC_SYS  + $BD_SYS"  | bc)
B_SC=$((BC_SC + BD_SC))

# ----- Escenario C: Pipeline completo (encrypt + decrypt) -----
echo -e "${BLU}[C]${NC} LZ77 + RC4 (encrypt)..."
read -r CE_WALL CE_USER CE_SYS <<< "$(IRON_TRIANGLE_KEY="$KEY" run_timed "$BINARY" -e "$INPUT" "$OUT_ENC")"
read -r CE_SC CE_WR CE_RD <<< "$(IRON_TRIANGLE_KEY="$KEY" count_syscalls "$BINARY" -e "$INPUT" "$OUT_ENC")"
C_ENC_SIZE=$(filesize "$OUT_ENC")

echo -e "${BLU}[C]${NC} LZ77 + RC4 (decrypt)..."
read -r CD_WALL CD_USER CD_SYS <<< "$(IRON_TRIANGLE_KEY="$KEY" run_timed "$BINARY" -d "$OUT_ENC" "$OUT_ENC_DEC")"
read -r CD_SC CD_WR CD_RD <<< "$(IRON_TRIANGLE_KEY="$KEY" count_syscalls "$BINARY" -d "$OUT_ENC" "$OUT_ENC_DEC")"

C_WALL=$(echo "scale=1; $CE_WALL + $CD_WALL" | bc)
C_USER=$(echo "scale=1; $CE_USER + $CD_USER" | bc)
C_SYS=$(echo  "scale=1; $CE_SYS  + $CD_SYS"  | bc)
C_SC=$((CE_SC + CD_SC))

# =========================================================================
# Ratios
# =========================================================================
B_RATIO=$(awk "BEGIN { printf \"%.1f\", $B_COMP_SIZE * 100 / $INPUT_SIZE }")
C_RATIO=$(awk "BEGIN { printf \"%.1f\", $C_ENC_SIZE  * 100 / $INPUT_SIZE }")

# Reducción de tiempo wall-clock A vs C
C_SPEEDUP=$(awk "BEGIN { printf \"%.1f\", (1 - $C_WALL / $A_WALL) * 100 }")

# =========================================================================
# Verificar integridad de los outputs
# =========================================================================
verify_integrity() {
    local scenario="$1"
    local file="$2"
    if diff -q "$INPUT" "$file" > /dev/null 2>&1; then
        echo -e "  ${GRN}✓ $scenario integridad OK${NC}"
    else
        echo -e "  ${RED}✗ $scenario INTEGRIDAD FALLIDA${NC}"
    fi
}
echo ""
verify_integrity "B (LZ77 roundtrip)" "$OUT_LZ77_DEC"
verify_integrity "C (pipeline roundtrip)" "$OUT_ENC_DEC"

# =========================================================================
# Escribir tabla en results.txt
# =========================================================================
{
printf "=== iron-triangle Benchmark Results ===\n"
printf "Fecha:          %s\n" "$(date '+%Y-%m-%d %H:%M:%S')"
printf "Archivo:        %s bytes (%.1f MB)\n" "$INPUT_SIZE" "$(echo "scale=1; $INPUT_SIZE / 1048576" | bc)"
printf "Sistema:        %s\n" "$(uname -srm)"
printf "CPU:            %s\n\n" "$(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"

printf "%-26s | %-12s | %-12s | %-14s\n" \
    "Métrica" "A. cp clásico" "B. Solo LZ77" "C. LZ77 + RC4"
printf "%s\n" "$(printf '─%.0s' {1..75})"

printf "%-26s | %-12s | %-12s | %-14s\n" \
    "Tamaño transmitido" \
    "$(human_size "$INPUT_SIZE")" \
    "$(human_size "$B_COMP_SIZE") ($B_RATIO%)" \
    "$(human_size "$C_ENC_SIZE") ($C_RATIO%)"

printf "%-26s | %-12s | %-12s | %-14s\n" \
    "Wall-clock (ms)" "$A_WALL" "$B_WALL" "$C_WALL"

printf "%-26s | %-12s | %-12s | %-14s\n" \
    "CPU user (ms)" "$A_USER" "$B_USER" "$C_USER"

printf "%-26s | %-12s | %-12s | %-14s\n" \
    "CPU system (ms)" "$A_SYS" "$B_SYS" "$C_SYS"

printf "%-26s | %-12s | %-12s | %-14s\n" \
    "Syscalls totales" "$A_SC" "$B_SC" "$C_SC"

printf "%s\n" "$(printf '─%.0s' {1..75})"
printf "Reducción wall-clock (A vs C): %s%%\n" "$C_SPEEDUP"
printf "\nArchivos intermedios:\n"
printf "  cp output:     %s bytes\n" "$A_OUT_SIZE"
printf "  LZ77:          %s bytes\n" "$B_COMP_SIZE"
printf "  LZ77+RC4:      %s bytes\n" "$C_ENC_SIZE"
} | tee "$RESULTS"

echo ""
echo -e "${GRN}=== Resultados escritos en $RESULTS ===${NC}"
echo "Copia los números a ANALYSIS.md para el entregable."

# Cleanup archivos temporales (mantener results.txt)
rm -f "$OUT_CP" "$OUT_LZ77" "$OUT_LZ77_DEC" "$OUT_ENC" "$OUT_ENC_DEC"
