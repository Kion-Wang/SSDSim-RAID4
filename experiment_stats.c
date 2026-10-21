#include "experiment_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

enum { WRITE_OP, READ_OP };
struct experiment_request { uint64_t count[2], total[2], maximum[2]; };
struct phase_stats {
    uint64_t count[2], total[2], maximum[2];
    uint64_t enqueue_count, same_total, all_total, same_max, all_max;
};
struct experiment_stats {
    FILE *latency, *backend, *mot1, *motivate, *borrow;
    struct phase_stats phase[2];
    uint64_t reads, writes, queries, hits, misses, parity_seq;
    uint64_t write_dep_requests, write_deps, write_wait[2], write_max_sum[2];
    uint64_t critical_total, critical_max, write_response_max;
    unsigned borrow_base;
    int borrow_base_set, borrow_pending, finished;
    uint64_t borrow_completed, borrow_window;
};
static void die(const char *reason) {
    fprintf(stderr,"Experiment statistics: %s\n",reason); exit(EXIT_FAILURE);
}
static void require(int condition, const char *reason) { if (!condition) die(reason); }
static void *allocate(size_t bytes) {
    void *p=calloc(1,bytes); if (!p) die("out of memory"); return p;
}
static uint64_t plus(uint64_t a, uint64_t b) {
    require(a<=UINT64_MAX-b,"counter overflow"); return a+b;
}
static void add(uint64_t *a, uint64_t b) { *a=plus(*a,b); }
#if defined(__GNUC__)
__attribute__((format(gnu_printf,2,3)))
#endif
static void emit(FILE *f, const char *format, ...) {
    va_list args; va_start(args,format); int n=vfprintf(f,format,args); va_end(args);
    if (n<0) die("output write failed");
}
static void directory(const char *path) {
#ifdef _WIN32
    int result=_mkdir(path);
#else
    int result=mkdir(path,0777);
#endif
    if (result && errno!=EEXIST) { perror(path); die("cannot create output directory"); }
}
static FILE *open_output(const char *folder, const char *scheme, const char *stem, const char *suffix) {
    char path[2048]; directory(folder);
    int n=scheme ? snprintf(path,sizeof(path),"%s/%s",folder,scheme) : snprintf(path,sizeof(path),"%s",folder);
    require(n>=0 && (size_t)n<sizeof(path),"directory path too long"); directory(path);
    n=scheme ? snprintf(path,sizeof(path),"%s/%s/%s%s",folder,scheme,stem,suffix)
             : snprintf(path,sizeof(path),"%s/%s%s",folder,stem,suffix);
    require(n>=0 && (size_t)n<sizeof(path),"output path too long");
    FILE *f=fopen(path,"w"); if (!f) { perror(path); die("cannot open output file"); } return f;
}
struct experiment_stats *experiment_stats_create(const char *scheme, const char *stem) {
    require(scheme && stem && *stem && !strpbrk(stem,"/\\"),"invalid output identity");
    struct experiment_stats *e=allocate(sizeof(*e));
    e->latency=open_output("latency",scheme,stem,".csv");
    e->backend=open_output("backend_statis",scheme,stem,".dat");
    if (!strcmp(scheme,"baseline")) {
        e->mot1=open_output("mot1",NULL,stem,".csv"); emit(e->mot1,"seq,key\n");
        e->motivate=open_output("motivate",NULL,stem,".csv"); emit(e->motivate,"arrival,latency\n");
    }
    if (!strcmp(scheme,"bfp")) {
        e->borrow=open_output("borrow",NULL,stem,".csv");
        emit(e->borrow,"completed,win,phase,cur_parity,cur_read,cap,parity_pct,borrow_out,borrow_ratio,time_ns,observed_completed\n");
    }
    return e;
}
struct experiment_request *experiment_request_create(unsigned operation) {
    require(operation<=READ_OP,"invalid host operation");
    return operation==WRITE_OP ? allocate(sizeof(struct experiment_request)) : NULL;
}
void experiment_read_query(struct experiment_stats *e, int hit) {
    add(&e->queries,1); add(hit ? &e->hits : &e->misses,1);
}
void experiment_parity_access(struct experiment_stats *e, unsigned stripe) {
    if (e->mot1) { add(&e->parity_seq,1); emit(e->mot1,"%llu,%u\n",(unsigned long long)e->parity_seq,stripe); }
}
void experiment_write_enqueue(struct experiment_stats *e, int recovery, uint64_t same, uint64_t all) {
    require(same<=all,"invalid read-pressure sample");
    struct phase_stats *p=&e->phase[recovery!=0];
    add(&p->enqueue_count,1); add(&p->same_total,same); add(&p->all_total,all);
    if (same>p->same_max) p->same_max=same;
    if (all>p->all_max) p->all_max=all;
}
void experiment_service(struct experiment_stats *e, int recovery, unsigned op, uint64_t wait,
                        struct experiment_request *owner) {
    require(op<=READ_OP,"invalid NAND operation");
    struct phase_stats *p=&e->phase[recovery!=0];
    add(&p->count[op],1); add(&p->total[op],wait);
    if (wait>p->maximum[op]) p->maximum[op]=wait;
    if (owner) {
        require(!recovery,"host dependency recorded during recovery");
        add(&owner->count[op],1); add(&owner->total[op],wait);
        if (wait>owner->maximum[op]) owner->maximum[op]=wait;
    }
}
void experiment_host_complete(struct experiment_stats *e, struct experiment_request *owner,
                              unsigned op, int64_t arrival, int64_t response) {
    require(op<=READ_OP && response>=arrival,"invalid host completion");
    uint64_t latency=(uint64_t)(response-arrival);
    emit(e->latency,"%llu\n",(unsigned long long)latency);
    if (op==READ_OP) {
        require(!owner,"read owns write-dependency record"); add(&e->reads,1);
        if (e->motivate) emit(e->motivate,"%lld,%llu\n",(long long)arrival,(unsigned long long)latency);
    } else {
        require(owner!=NULL,"write lacks dependency record"); add(&e->writes,1);
        if (owner->count[READ_OP]) add(&e->write_dep_requests,1);
        add(&e->write_deps,owner->count[READ_OP]);
        for (unsigned i=0;i<2;++i) { add(&e->write_wait[i],owner->total[i]); add(&e->write_max_sum[i],owner->maximum[i]); }
        uint64_t critical=owner->maximum[0]>owner->maximum[1] ? owner->maximum[0] : owner->maximum[1];
        add(&e->critical_total,critical);
        if (critical>e->critical_max) e->critical_max=critical;
        if (latency>e->write_response_max) e->write_response_max=latency;
    }
    free(owner);
}
static void borrow_row(struct experiment_stats *e, uint64_t completed, uint64_t window, unsigned phase,
                       int64_t now, uint64_t observed, unsigned w, unsigned r) {
    uint64_t cap=(uint64_t)w+r, out=e->borrow_base>w ? e->borrow_base-w : 0;
    emit(e->borrow,"%llu,%llu,%u,%u,%u,%llu,%.6f,%llu,%.6f,%lld,%llu\n",
         (unsigned long long)completed,(unsigned long long)window,phase,w,r,(unsigned long long)cap,
         cap ? (double)w/cap : 0,(unsigned long long)out,
         e->borrow_base ? (double)out/e->borrow_base : 0,(long long)now,(unsigned long long)observed);
}
void experiment_borrow_window(struct experiment_stats *e, uint64_t completed, uint64_t window,
                             int64_t now, unsigned w, unsigned r, int proposed) {
    require(e->borrow!=NULL,"borrow observation on another scheme");
    if (!e->borrow_base_set) { e->borrow_base=w; e->borrow_base_set=1; }
    borrow_row(e,completed,window,0,now,completed,w,r);
    if (proposed) {
        require(!e->borrow_pending,"overlapping borrow observations");
        e->borrow_pending=1; e->borrow_completed=completed; e->borrow_window=window;
    }
}
void experiment_borrow_commit(struct experiment_stats *e, uint64_t completed, int64_t now, unsigned w, unsigned r) {
    require(e->borrow && e->borrow_pending,"borrow commit without proposal");
    borrow_row(e,e->borrow_completed,e->borrow_window,1,now,completed,w,r); e->borrow_pending=0;
}
static double average(uint64_t sum, uint64_t count) { return count ? (double)sum/count : 0; }
static void backend_phase(FILE *f, const struct phase_stats *p, const char *prefix) {
#define U(key,value) emit(f,"%s" key " %llu\n",prefix,(unsigned long long)(value))
#define D(key,value) emit(f,"%s" key " %.6f\n",prefix,(double)(value))
    U("read_subreq_count",p->count[READ_OP]); U("write_subreq_count",p->count[WRITE_OP]);
    D("avg_read_backend_wait",average(p->total[READ_OP],p->count[READ_OP]));
    D("avg_write_backend_wait",average(p->total[WRITE_OP],p->count[WRITE_OP]));
    U("max_read_backend_wait",p->maximum[READ_OP]); U("max_write_backend_wait",p->maximum[WRITE_OP]);
    U("write_enqueue_samples",p->enqueue_count);
    D("avg_write_enqueue_readq_same_ch",average(p->same_total,p->enqueue_count));
    D("avg_write_enqueue_readq_all_ch",average(p->all_total,p->enqueue_count));
    U("max_write_enqueue_readq_same_ch",p->same_max); U("max_write_enqueue_readq_all_ch",p->all_max);
    U("read_backend_wait_total",p->total[READ_OP]); U("write_backend_wait_total",p->total[WRITE_OP]);
    U("write_enqueue_readq_same_ch_total",p->same_total); U("write_enqueue_readq_all_ch_total",p->all_total);
#undef U
#undef D
}
static void close_file(FILE **f) {
    if (*f) { int failed=ferror(*f); if (fclose(*f)) failed=1; *f=NULL; if (failed) die("output flush/close failed"); }
}
void experiment_stats_finish(struct experiment_stats *e) {
    require(e && !e->finished && !e->borrow_pending,"unfinished/duplicate statistics finalization");
    require(e->queries==plus(e->hits,e->misses),"logical read accounting mismatch");
    FILE *f=e->backend;
#define U(key,value) emit(f,key " %llu\n",(unsigned long long)(value))
#define D(key,value) emit(f,key " %.6f\n",(double)(value))
    U("total_read_requests",e->reads); U("total_read_subrequests",e->queries);
    U("read_cache_hits",e->hits); U("backend_read_misses",e->misses); U("backend_read_accesses",e->queries);
    D("backend_read_miss_ratio",average(e->misses,e->queries));
    backend_phase(f,&e->phase[0],"");
    U("write_req_backend_summary_count",e->writes); U("write_req_with_backend_read_dep_count",e->write_dep_requests);
    D("avg_write_req_backend_read_dep",average(e->write_deps,e->writes));
    D("avg_write_req_backend_read_wait",average(e->write_wait[READ_OP],e->writes));
    D("avg_write_req_backend_write_wait",average(e->write_wait[WRITE_OP],e->writes));
    D("avg_write_req_backend_read_wait_max",average(e->write_max_sum[READ_OP],e->writes));
    D("avg_write_req_backend_write_wait_max",average(e->write_max_sum[WRITE_OP],e->writes));
    D("avg_write_req_backend_critical_wait",average(e->critical_total,e->writes));
    U("max_write_req_backend_critical_wait",e->critical_max); U("max_write_req_response_time",e->write_response_max);
    U("write_req_backend_read_dep_total",e->write_deps);
    U("write_req_backend_read_wait_total",e->write_wait[READ_OP]); U("write_req_backend_write_wait_total",e->write_wait[WRITE_OP]);
    U("write_req_backend_read_wait_max_total",e->write_max_sum[READ_OP]); U("write_req_backend_write_wait_max_total",e->write_max_sum[WRITE_OP]);
    U("write_req_backend_critical_wait_total",e->critical_total);
    backend_phase(f,&e->phase[1],"recovery_");
    close_file(&e->backend); close_file(&e->latency); close_file(&e->mot1); close_file(&e->motivate); close_file(&e->borrow);
    e->finished=1;
#undef U
#undef D
}
void experiment_stats_destroy(struct experiment_stats *e) {
    if (!e) return;
    require(e->finished,"statistics destroyed before finalization"); free(e);
}
