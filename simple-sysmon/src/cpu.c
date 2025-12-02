#include "cpu.h"
#include "utils.h"
#include <unistd.h>

float get_cpu_usage() {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;

    char buffer[256];
    fgets(buffer, sizeof(buffer), fp);
    fclose(fp);

    long user, nice, system, idle, iowait, irq, softirq;
    sscanf(buffer, "cpu %ld %ld %ld %ld %ld %ld %ld",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq);

    long idle1 = idle + iowait;
    long nonidle1 = user + nice + system + irq + softirq;
    long total1 = idle1 + nonidle1;

    usleep(100000);

    fp = fopen("/proc/stat", "r");
    fgets(buffer, sizeof(buffer), fp);
    fclose(fp);

    long user2, nice2, system2, idle2, iowait2, irq2, softirq2;
    sscanf(buffer, "cpu %ld %ld %ld %ld %ld %ld %ld",
           &user2, &nice2, &system2, &idle2, &iowait2, &irq2, &softirq2);

    long idle_diff = (idle2 + iowait2) - idle1;
    long total2 = (idle2 + iowait2) + (user2 + nice2 + system2 + irq2 + softirq2);
    long total_diff = total2 - total1;

    return (float)(total_diff - idle_diff) / total_diff * 100.0f;
}
