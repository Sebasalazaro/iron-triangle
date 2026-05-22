#include "io.h"

#include <fcntl.h>      /* open() flags: O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC */
#include <unistd.h>     /* read(), write(), close() */
#include <sys/stat.h>   /* fstat() — query file metadata without path lookup */
#include <stdlib.h>     /* malloc(), free() */
#include <string.h>     /* memcpy() */
#include <stdio.h>      /* perror(), fprintf() */
#include <errno.h>      /* errno values after syscall failures */

/*
 * read_file — loads an entire file into a heap buffer.
 *
 * Uses fstat() to get the file size upfront, then reads in PAGE_SIZE chunks
 * to minimize the number of context switches between user and kernel space.
 * The 4096-byte chunk matches the x86 page size, so each read() can be served
 * by the kernel's page cache with optimal alignment.
 *
 * Returns 0 on success, -1 on error. Sets *buf and *len on success.
 */
int read_file(const char *path, uint8_t **buf, size_t *len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("read_file: open");
        return -1;
    }

    /* fstat() queries the inode directly — no second path resolution */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("read_file: fstat");
        close(fd);
        return -1;
    }

    size_t file_size = (size_t)st.st_size;
    uint8_t *data = malloc(file_size);
    if (!data) {
        perror("read_file: malloc");
        close(fd);
        return -1;
    }

    /*
     * Read in 4096-byte pages — aligns to the kernel's page cache granularity.
     * Each read() issues one context switch; batching by page size minimizes
     * the total number of transitions from user space to kernel space.
     */
    uint8_t page_buf[PAGE_SIZE];
    size_t total_read = 0;
    ssize_t n;

    while ((n = read(fd, page_buf, sizeof(page_buf))) > 0) {
        if (total_read + (size_t)n > file_size) {
            /* File grew between fstat and read — guard against overflow */
            fprintf(stderr, "read_file: file size changed during read\n");
            free(data);
            close(fd);
            return -1;
        }
        memcpy(data + total_read, page_buf, (size_t)n);
        total_read += (size_t)n;
    }

    if (n < 0) {
        perror("read_file: read");
        free(data);
        close(fd);
        return -1;
    }

    close(fd);
    *buf  = data;
    *len  = total_read;
    return 0;
}

/*
 * write_file — flushes the entire in-RAM buffer to disk.
 *
 * The entire pipeline (compress + encrypt) runs in RAM first. Only when the
 * result is ready do we issue a single write() to the kernel. This minimizes
 * the number of I/O operations and avoids partial writes of intermediate state.
 *
 * O_TRUNC truncates the file to zero before writing so stale bytes never
 * appear at the end if the new content is shorter than the old file.
 *
 * Returns 0 on success, -1 on error.
 */
int write_file(const char *path, const uint8_t *buf, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("write_file: open");
        return -1;
    }

    /* Single write() — the complete pipeline result hits the kernel in one shot */
    ssize_t written = write(fd, buf, len);
    if (written < 0) {
        perror("write_file: write");
        close(fd);
        return -1;
    }

    if ((size_t)written != len) {
        fprintf(stderr, "write_file: short write (%zd of %zu bytes)\n",
                written, len);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}
