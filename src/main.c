#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s -c <input> <output>   Compress (future)\n"
        "  %s -e <input> <output>   Compress + Encrypt (future)\n"
        "  %s -d <input> <output>   Decrypt + Decompress (future)\n"
        "\nCommit 1: raw copy to verify syscall I/O layer.\n",
        prog, prog, prog);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        usage(argv[0]);
    }

    const char *mode        = argv[1];
    const char *input_path  = argv[2];
    const char *output_path = argv[3];

    if (strcmp(mode, "-c") != 0 &&
        strcmp(mode, "-e") != 0 &&
        strcmp(mode, "-d") != 0) {
        usage(argv[0]);
    }

    uint8_t *buf = NULL;
    size_t   len = 0;

    /* Stage 1: Load entire file into RAM using page-aligned reads */
    if (read_file(input_path, &buf, &len) < 0) {
        return 1;
    }
    fprintf(stderr, "[io] read  %zu bytes from '%s'\n", len, input_path);

    /* Stage 2: Compress in RAM     — placeholder, Commit 2 */
    /* Stage 3: Encrypt in RAM      — placeholder, Commit 4 */

    /* Stage 4: Single write() — entire pipeline result to disk */
    if (write_file(output_path, buf, len) < 0) {
        free(buf);
        return 1;
    }
    fprintf(stderr, "[io] wrote %zu bytes to   '%s'\n", len, output_path);

    free(buf);
    return 0;
}
