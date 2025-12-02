#include "memory.h"
#include "utils.h"

void get_memory_usage(long *total, long *used, long *free_mem) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;

    long memtotal, memfree, buffers, cached;
    char label[32];

    while (fscanf(fp, "%31s %ld kB\n", label, &memtotal) != EOF) {
        if (strcmp(label, "MemTotal:") == 0) *total = memtotal / 1024;
        if (strcmp(label, "MemFree:") == 0) *free_mem = memfree / 1024;
    }

    fclose(fp);

    *used = *total - *free_mem;
}
