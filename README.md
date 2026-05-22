# iron-triangle

Pipeline de compresión + encriptación en C puro para Linux.  
Proyecto final de Sistemas Operativos — EAFIT.

---

## El pipeline

```
Archivo de entrada
      │
      ▼
 [read() × N]          ← lecturas de 4096 bytes (tamaño de página x86)
      │
      ▼
 Buffer en RAM
      │
      ├─► [LZ77 compress]   ← sliding window 4096 B, look-ahead 16 B
      │
      ├─► [RC4 encrypt]     ← KSA + PRGA, clave borrada con explicit_bzero
      │
      ▼
 [write() × 1]         ← UNA sola llamada al disco
      │
      ▼
 Archivo de salida
```

**Todo el procesamiento ocurre en RAM. Solo hay una llamada `write()` final.**

---

## Regla Arquitectónica 6 — Orden obligatorio: comprimir → encriptar

La encriptación transforma los datos en ruido pseudoaleatorio de **alta entropía**.  
Un compresor busca patrones repetitivos: si encriptas primero, no quedan patrones y  
la compresión es imposible (el archivo incluso puede crecer por la sobrecarga del  
algoritmo). El orden correcto es:

> **Comprimir primero → Encriptar después**

---

## Gestión segura de la clave

1. La clave **nunca** está en el código fuente ni en `argv`.  
2. Se lee con `getpass()` (sin eco en terminal).  
3. La página de RAM que la contiene se bloquea con `mlock()` para que el kernel  
   no la mande al Swap (evita que quede en disco).  
4. Tras inicializar RC4, se borra con `explicit_bzero()` inmediatamente.

---

## Build

```bash
make          # compila iron-triangle
make test     # roundtrip: comprime → descomprime → diff
make benchmark
make clean
```

**Requisitos:** gcc, Linux (syscalls POSIX). Sin dependencias externas.

---

## Uso

```bash
# Comprimir + encriptar
./iron-triangle -e input.txt input.txt.ite

# Desencriptar + descomprimir
./iron-triangle -d input.txt.ite output.txt

# Verificar integridad
diff input.txt output.txt && echo "OK"
```

---

## Restricciones técnicas

| Parámetro | Valor | Razón |
|---|---|---|
| Buffer de I/O | 4096 bytes | Tamaño de página x86, alineado al page cache |
| Sliding window LZ77 | 4096 bytes | Coherente con el tamaño de página |
| Look-ahead buffer | 16 bytes | Balance compresión/velocidad |
| Token LZ77 | 3 bytes (offset 12 b \| length 4 b \| literal 8 b) | Cubre toda la ventana con 12 bits |
| Llamadas `write()` al disco | 1 por archivo | Todo el pipeline vive en RAM |

---

## Estructura

```
iron-triangle/
├── Makefile
├── README.md
├── ANALYSIS.md          # Tabla de benchmark con números reales
├── SUSTENTACION.md      # Respuestas a preguntas de defensa oral
├── src/
│   ├── main.c           # Orquestación del pipeline
│   ├── io.c / io.h      # Syscalls open/read/write, buffer 4096
│   ├── lz77.c / lz77.h  # Compresor/descompresor LZ77
│   └── rc4.c / rc4.h    # Cifrador RC4 + gestión segura de clave
├── tests/
│   └── test_roundtrip.sh
└── benchmark/
    ├── benchmark.sh
    └── results.txt
```
