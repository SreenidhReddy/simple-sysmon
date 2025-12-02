/*
 Simple System Monitor (C, ncurses)
 - Parses /proc for cpu, mem, net and process info
 - Interactive ncurses UI by default
 - Headless snapshot mode: run with `--snapshot` to print one snapshot and exit (used by CI)
 - Build: gcc -O2 -Wall -o sysmon src/system_monitor.c -lncurses
*/

#define _GNU_SOURCE
#include <ncurses.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>

#define REFRESH_SECS 1
#define MAX_PROCS  64
#define CMDLINE_LEN 256

typedef struct {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
} cpu_stat_t;

typedef struct {
    pid_t pid;
    long rss_kb;
    char cmd[CMDLINE_LEN];
} proc_info_t;

/* Read first cpu line from /proc/stat */
int read_cpu_stat(cpu_stat_t *s) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    char buf[512];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return -1; }
    char cpu_label[16];
    unsigned long long guest=0, guest_nice=0;
    // parse at most 10 fields (guest/guest_nice optional)
    sscanf(buf, "%15s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
           cpu_label,
           &s->user, &s->nice, &s->system, &s->idle,
           &s->iowait, &s->irq, &s->softirq, &s->steal,
           &guest, &guest_nice);
    fclose(f);
    return 0;
}

double compute_cpu_usage(const cpu_stat_t *old, const cpu_stat_t *cur) {
    unsigned long long old_idle = old->idle + old->iowait;
    unsigned long long cur_idle = cur->idle + cur->iowait;

    unsigned long long old_non_idle = old->user + old->nice + old->system + old->irq + old->softirq + old->steal;
    unsigned long long cur_non_idle = cur->user + cur->nice + cur->system + cur->irq + cur->softirq + cur->steal;

    unsigned long long old_total = old_idle + old_non_idle;
    unsigned long long cur_total = cur_idle + cur_non_idle;

    unsigned long long totald = cur_total - old_total;
    unsigned long long idled = cur_idle - old_idle;

    if (totald == 0) return 0.0;
    double cpu_percentage = (double)(totald - idled) * 100.0 / (double)totald;
    return cpu_percentage;
}

/* Read meminfo */
int read_meminfo(unsigned long long *total_kb, unsigned long long *free_kb, unsigned long long *available_kb) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[256];
    *total_kb = *free_kb = *available_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %llu kB", total_kb) == 1) continue;
        if (sscanf(line, "MemFree: %llu kB", free_kb) == 1) continue;
        if (sscanf(line, "MemAvailable: %llu kB", available_kb) == 1) continue;
    }
    fclose(f);
    return 0;
}

/* Disk usage for root mount */
int read_disk_usage(unsigned long long *total_kb, unsigned long long *used_kb, unsigned long long *avail_kb) {
    struct statvfs st;
    if (statvfs("/", &st) != 0) return -1;
    unsigned long long block_size = st.f_frsize;
    unsigned long long total = (st.f_blocks * block_size) / 1024;
    unsigned long long avail = (st.f_bavail * block_size) / 1024;
    unsigned long long used = total - avail;
    *total_kb = total;
    *used_kb = used;
    *avail_kb = avail;
    return 0;
}

/* Read network totals from /proc/net/dev - sum across interfaces (skip lo) */
int read_net_totals(unsigned long long *rx_bytes, unsigned long long *tx_bytes) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return -1;
    char line[512];
    *rx_bytes = *tx_bytes = 0;
    // skip two header lines
    fgets(line, sizeof(line), f);
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f)) {
        char iface[64];
        unsigned long long rbytes=0, tbytes=0;
        // crude parse: interface: <rx> <...fields...> <tx at field 9>
        char *colon = strchr(line, ':');
        if (!colon) continue;
        // extract iface
        int i = 0;
        char *p = line;
        while (p < colon && i < (int)sizeof(iface)-1) {
            if (*p != ' ' && *p != '\t' && *p != '\n') iface[i++] = *p;
            p++;
        }
        iface[i] = '\0';
        // parse numbers after ':'
        unsigned long long fields[16] = {0};
        int idx = 0;
        char *tok = strtok(colon+1, " \t\n");
        while (tok && idx < 16) {
            fields[idx++] = strtoull(tok, NULL, 10);
            tok = strtok(NULL, " \t\n");
        }
        if (idx >= 9) {
            rbytes = fields[0];
            tbytes = fields[8];
        }
        if (strcmp(iface, "lo") != 0) {
            *rx_bytes += rbytes;
            *tx_bytes += tbytes;
        }
    }
    fclose(f);
    return 0;
}

/* Read processes: scan /proc for numeric directories, read cmdline and stat to get rss */
int read_processes(proc_info_t *procs, int *count) {
    DIR *d = opendir("/proc");
    if (!d) return -1;

    struct dirent *ent;
    int idx = 0;

    while ((ent = readdir(d)) != NULL && idx < MAX_PROCS) {
        if (!isdigit(ent->d_name[0]))
            continue;

        pid_t pid = (pid_t)atoi(ent->d_name);
        char path[256];
        char cmd[CMDLINE_LEN] = {0};
        long rss_pages = 0;

        /* Read cmdline */
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE *f = fopen(path, "r");
        if (f) {
            size_t n = fread(cmd, 1, sizeof(cmd) - 1, f);
            fclose(f);
            if (n > 0) {
                for (size_t i = 0; i < n; ++i)
                    if (cmd[i] == '\0') cmd[i] = ' ';
                cmd[n] = '\0';
            }
        }

        /* Fallback: comm if cmdline empty */
        if (cmd[0] == '\0') {
            snprintf(path, sizeof(path), "/proc/%d/comm", pid);
            FILE *fc = fopen(path, "r");
            if (fc) {
                fgets(cmd, sizeof(cmd), fc);
                fclose(fc);
                size_t len = strlen(cmd);
                if (len > 0 && cmd[len - 1] == '\n')
                    cmd[len - 1] = '\0';
            }
        }

        /* Read RSS from stat */
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE *fs = fopen(path, "r");
        if (fs) {
            char buf[1024];
            if (fgets(buf, sizeof(buf), fs)) {
                /* stat format: pid (comm) state ... rss is field #24 (index 23) */
                char *rp = strrchr(buf, ')');
                if (rp) {
                    int field = 3;
                    char *tok = strtok(rp + 1, " ");
                    while (tok && field <= 24) {
                        if (field == 24) {
                            rss_pages = atol(tok);
                            break;
                        }
                        tok = strtok(NULL, " ");
                        field++;
                    }
                }
            }
            fclose(fs);
        }

        long page_kb = sysconf(_SC_PAGESIZE) / 1024;
        long rss_kb = rss_pages * page_kb;

        /* Store result */
        procs[idx].pid = pid;
        procs[idx].rss_kb = rss_kb;
        strncpy(procs[idx].cmd, cmd, CMDLINE_LEN - 1);
        procs[idx].cmd[CMDLINE_LEN - 1] = '\0';

        idx++;
    }

    closedir(d);
    *count = idx;
    return 0;
}

int cmp_procs(const void *a, const void *b) {
    const proc_info_t *A = a;
    const proc_info_t *B = b;
    return (B->rss_kb - A->rss_kb);
}

/* Print one snapshot (headless mode) */
void print_snapshot(void) {
    cpu_stat_t prev = {0}, cur = {0};

    if (read_cpu_stat(&prev) != 0)
        return;

    sleep(1);
    if (read_cpu_stat(&cur) != 0)
        return;

    double cpu_pct = compute_cpu_usage(&prev, &cur);

    /* Memory */
    unsigned long long mem_total = 0, mem_free = 0, mem_avail = 0;
    read_meminfo(&mem_total, &mem_free, &mem_avail);
    unsigned long long mem_used = mem_total - mem_avail;
    double mem_pct = (mem_total == 0) ? 0 : (double)mem_used * 100.0 / mem_total;

    /* Disk */
    unsigned long long disk_total = 0, disk_used = 0, disk_avail = 0;
    read_disk_usage(&disk_total, &disk_used, &disk_avail);
    double disk_pct = (disk_total == 0) ? 0 : (double)disk_used * 100.0 / disk_total;

    /* Network */
    unsigned long long rx0=0, tx0=0, rx1=0, tx1=0;
    read_net_totals(&rx0, &tx0);
    sleep(1);
    read_net_totals(&rx1, &tx1);
    unsigned long long drx = (rx1 > rx0 ? rx1 - rx0 : 0);
    unsigned long long dtx = (tx1 > tx0 ? tx1 - tx0 : 0);

    /* Processes */
    proc_info_t procs[MAX_PROCS];
    int count = 0;
    read_processes(procs, &count);
    qsort(procs, count, sizeof(proc_info_t), cmp_procs);

    /* Print snapshot */
    printf("CPU: %.2f %%\n", cpu_pct);
    printf("Memory: %.2f %% (%llu MB used / %llu MB total)\n",
           mem_pct, mem_used/1024, mem_total/1024);
    printf("Disk: %.2f %% (%llu MB used / %llu MB total)\n",
           disk_pct, disk_used/1024, disk_total/1024);
    printf("Network: RX %llu B/s, TX %llu B/s\n", drx, dtx);

    printf("Top Processes by RSS:\n");
    printf("%-6s %-10s %s\n", "PID", "RSS(KB)", "COMMAND");

    int show = (count < 10 ? count : 10);
    for (int i = 0; i < show; i++) {
        printf("%-6d %-10ld %.60s\n",
               procs[i].pid,
               procs[i].rss_kb,
               procs[i].cmd);
    }
}

int main(int argc, char **argv) {
    int headless = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--snapshot") == 0) headless = 1;
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("sysmon [--snapshot]\\n\\n--snapshot : print one snapshot to stdout and exit (non-interactive)\\n");
            return 0;
        }
    }

    if (headless) {
        print_snapshot();
        return 0;
    }

    /* Interactive ncurses UI */
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE); // non-blocking input
    curs_set(0);

    cpu_stat_t prev_cpu = {0}, cur_cpu = {0};
    if (read_cpu_stat(&prev_cpu) != 0) {
        endwin();
        fprintf(stderr, "Failed to read /proc/stat\\n");
        return 1;
    }

    unsigned long long prev_rx = 0, prev_tx = 0;
    read_net_totals(&prev_rx, &prev_tx);

    while (1) {
        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;

        for (int i = 0; i < REFRESH_SECS*10; ++i) {
            usleep(100000);
            int c2 = getch();
            if (c2 == 'q' || c2 == 'Q') {
                endwin();
                return 0;
            }
        }

        if (read_cpu_stat(&cur_cpu) != 0) break;
        double cpu_pct = compute_cpu_usage(&prev_cpu, &cur_cpu);
        prev_cpu = cur_cpu;

        unsigned long long mem_total=0, mem_free=0, mem_avail=0;
        read_meminfo(&mem_total, &mem_free, &mem_avail);
        unsigned long long mem_used = (mem_total > mem_avail) ? (mem_total - mem_avail) : 0;
        double mem_pct = (mem_total == 0) ? 0.0 : (double)mem_used * 100.0 / (double)mem_total;

        unsigned long long disk_total=0, disk_used=0, disk_avail=0;
        read_disk_usage(&disk_total, &disk_used, &disk_avail);
        double disk_pct = (disk_total == 0) ? 0.0 : (double)disk_used * 100.0 / (double)disk_total;

        unsigned long long rx=0, tx=0;
        read_net_totals(&rx, &tx);
        unsigned long long d_rx = (rx >= prev_rx) ? (rx - prev_rx) : 0;
        unsigned long long d_tx = (tx >= prev_tx) ? (tx - prev_tx) : 0;
        prev_rx = rx; prev_tx = tx;

        proc_info_t procs[MAX_PROCS];
        int pcount = 0;
        read_processes(procs, &pcount);
        qsort(procs, pcount, sizeof(proc_info_t), cmp_procs);

        erase();
        int row = 0;
        mvprintw(row++, 0, "Simple System Monitor - refresh every %d s  (press q to quit)", REFRESH_SECS);
        mvhline(row++, 0, '-', COLS);

        mvprintw(row++, 0, "CPU Usage: %.2f %%", cpu_pct);

        mvprintw(row++, 0, "Memory: %.2f %%  Used: %llu MB  Total: %llu MB",
                 mem_pct, mem_used/1024, mem_total/1024);

        mvprintw(row++, 0, "Disk (/): %.2f %%  Used: %llu MB  Total: %llu MB",
                 disk_pct, disk_used/1024, disk_total/1024);

        mvprintw(row++, 0, "Network: RX: %llu B/s  TX: %llu B/s", d_rx / REFRESH_SECS, d_tx / REFRESH_SECS);

        mvhline(row++, 0, '-', COLS);
        mvprintw(row++, 0, "Top processes by RSS (memory):");
        mvprintw(row++, 0, "%-6s %-8s %-8s %s", "PID", "RSS(KB)", "RSS(MB)", "COMMAND");
        mvhline(row++, 0, '-', COLS);

        int show = pcount;
        int maxrows = LINES - row - 1;
        if (show > maxrows) show = maxrows;
        for (int i = 0; i < show; ++i) {
            mvprintw(row + i, 0, "%-6d %-8ld %-8ld %.60s",
                     procs[i].pid, (long)procs[i].rss_kb, (long)(procs[i].rss_kb/1024), procs[i].cmd);
        }

        refresh();
    }

    endwin();
    return 0;
}
