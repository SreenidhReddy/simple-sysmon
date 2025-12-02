#include "disk.h"
#include "utils.h"

long get_disk_reads() {
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) return -1;

    char name[32];
    long reads;

    while (fscanf(fp, "%*d %*d %31s %ld %*d %*d %*d %*d %*d %*d %*d %*d",
                  name, &reads) == 2) {
        if (strcmp(name, "sda") == 0) {
            fclose(fp);
            return reads;
        }
    }

    fclose(fp);
    return -1;
}

long get_disk_writes() {
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) return -1;

    char name[32];
    long writes, dummy;

    while (fscanf(fp, "%*d %*d %31s %*d %*d %*d %*d %ld",
                  name, &writes) == 2) {
        if (strcmp(name, "sda") == 0) {
            fclose(fp);
            return writes;
        }
    }

    fclose(fp);
    return -1;
}
