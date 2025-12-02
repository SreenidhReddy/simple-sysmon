#include "cpu.h"
#include "memory.h"
#include "disk.h"
#include "utils.h"

int main(int argc, char *argv[]) {
    int snapshot = (argc > 1 && strcmp(argv[1], "--snapshot") == 0);

    float cpu = get_cpu_usage();

    long total, used, free_mem;
    get_memory_usage(&total, &used, &free_mem);

    long reads = get_disk_reads();
    long writes = get_disk_writes();

    printf("=== SYSTEM SNAPSHOT ===\n");
    printf("CPU Usage     : %.2f%%\n", cpu);
    printf("Memory Used   : %ld MB / %ld MB\n", used, total);
    printf("Disk Reads    : %ld\n", reads);
    printf("Disk Writes   : %ld\n", writes);

    return 0;
}
