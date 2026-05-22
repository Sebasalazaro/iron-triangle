#ifndef IO_H
#define IO_H

#include <stddef.h>
#include <stdint.h>

/* Standard x86 memory page size — aligns I/O buffers to hardware page boundaries */
#define PAGE_SIZE 4096

/* Read entire file into a heap-allocated buffer. Caller must free(*buf). */
int read_file(const char *path, uint8_t **buf, size_t *len);

/* Write entire buffer to file in a single write() syscall. */
int write_file(const char *path, const uint8_t *buf, size_t len);

#endif /* IO_H */
