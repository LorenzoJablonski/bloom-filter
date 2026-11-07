#ifndef PROCSTAT_H
#define PROCSTAT_H

/*
 * Process memory counters used by the benchmark and the I/O experiment to
 * show when a structure no longer fits in RAM (paging): page faults of this
 * process (getrusage) and its current resident set (/proc/self/statm).
 * Reading them costs a system call: call only outside timed loops.
 */

#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

typedef struct {
    long majflt; /* faults that needed I/O (swap-in, file pages not cached) */
    long minflt; /* faults served from memory (first touch, reclaimable pages) */
} proc_faults_t;

static inline proc_faults_t proc_faults(void)
{
    struct rusage ru;
    proc_faults_t f = { -1, -1 };
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        f.majflt = ru.ru_majflt;
        f.minflt = ru.ru_minflt;
    }
    return f;
}

/* Resident set size in KiB, or -1 if unavailable. */
static inline long proc_rss_kb(void)
{
    long size = 0, resident = -1;
    FILE *fh = fopen("/proc/self/statm", "r");
    if (!fh) return -1;
    if (fscanf(fh, "%ld %ld", &size, &resident) != 2) resident = -1;
    fclose(fh);
    long page = sysconf(_SC_PAGESIZE);
    return (resident < 0 || page <= 0) ? -1 : resident * (page / 1024);
}

#endif /* PROCSTAT_H */
