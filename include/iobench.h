#ifndef IOBENCH_H
#define IOBENCH_H

#include <stddef.h>

typedef struct {
    const char *type;       /* dataset type: uuid/cpf/cnpj/url */
    size_t n_override;      /* 0 = all of Dins */
    double p_target;        /* target FPR used to size each table's filter */
    unsigned tables;        /* T: number of on-disk tables (LSM batches) */
    double neg_ratio;       /* fraction of queries drawn from Dinexist */
    size_t queries;         /* queries per repetition */
    unsigned reps;          /* reported repetitions (+1 warm-up) */
    int direct;             /* 1 = O_DIRECT (default), 0 = page cache */
    size_t value_size;      /* synthetic payload per record, bytes */
    const char *store_dir;  /* where the table files live (pick the device here) */
    const char *out_path;   /* CSV, NULL = stdout */
    const char *filter;     /* one gatekeeper ("none" or a filter); NULL = all */
    int build_only;         /* build the missing tables and exit, measuring nothing */
} iobench_cfg_t;

int iobench_run(const iobench_cfg_t *cfg);

#endif /* IOBENCH_H */
