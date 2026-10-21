#include "bfp_cache.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

struct node {
    unsigned key, mask;
    uint64_t count, version, *values;
    struct node *hash_next, *prev, *next;
};
struct table {
    struct node **buckets, *head, *tail;
    size_t bucket_count;
    unsigned count, limit;
};
struct bfp_cache {
    struct table data, ghost, read_history, parity_history;
    struct bfp_config cfg;
    struct bfp_stats stats;
    unsigned slots, sectors, initial_w, w, r, lo, hi, hysteresis, step, goal;
    int verify, pending, ema_valid;
    double ema;
    uint64_t next_boundary, window_reads, window_parity;
};
static void die(const char *message) {
    fprintf(stderr,"BFP: %s\n",message); exit(EXIT_FAILURE);
}
static void require(int condition, const char *message) { if (!condition) die(message); }
static void *allocate(size_t n, size_t bytes) {
    require(bytes && n<=SIZE_MAX/bytes,"allocation size overflow");
    void *p=calloc(n,bytes); if (!p) die("out of memory"); return p;
}
static uint64_t add(uint64_t a, uint64_t b) {
    require(a<=UINT64_MAX-b,"counter/profit overflow"); return a+b;
}
static size_t bucket(const struct table *t, unsigned key) {
    return ((uint64_t)key*UINT64_C(11400714819323198485)) & (t->bucket_count-1);
}
static void table_init(struct table *t, unsigned limit) {
    t->limit=limit; t->bucket_count=1;
    while (t->bucket_count<(size_t)limit) {
        require(t->bucket_count<=SIZE_MAX/2,"hash size overflow"); t->bucket_count*=2;
    }
    t->buckets=allocate(t->bucket_count,sizeof(*t->buckets));
}
static struct node *lookup(const struct table *t, unsigned key) {
    for (struct node *n=t->buckets[bucket(t,key)];n;n=n->hash_next) if (n->key==key) return n;
    return NULL;
}
static void unlink_lru(struct table *t, struct node *n) {
    if (n->prev) n->prev->next=n->next; else t->head=n->next;
    if (n->next) n->next->prev=n->prev; else t->tail=n->prev;
}
static void touch(struct table *t, struct node *n) {
    unlink_lru(t,n); n->prev=NULL; n->next=t->head;
    if (t->head) t->head->prev=n; else t->tail=n;
    t->head=n;
}
static void remove_node(struct table *t, struct node *n) {
    struct node **link=&t->buckets[bucket(t,n->key)];
    while (*link && *link!=n) link=&(*link)->hash_next;
    require(*link==n && t->count,"corrupt hash/LRU removal");
    *link=n->hash_next; unlink_lru(t,n); --t->count; free(n->values); free(n);
}
static struct node *insert(struct table *t, unsigned key) {
    require(t->count<t->limit && !lookup(t,key),"invalid/duplicate insertion");
    struct node *n=allocate(1,sizeof(*n)); n->key=key;
    size_t slot=bucket(t,key); n->hash_next=t->buckets[slot]; t->buckets[slot]=n;
    n->next=t->head; if (t->head) t->head->prev=n; else t->tail=n;
    t->head=n; ++t->count; return n;
}
static void history_note(struct table *t, unsigned key) {
    if (!t->limit) return;
    struct node *n=lookup(t,key);
    if (n) touch(t,n);
    else { if (t->count==t->limit) remove_node(t,t->tail); n=insert(t,key); }
    n->count=add(n->count,1);
}
static void clear_table(struct table *t) { while (t->tail) remove_node(t,t->tail); }
void bfp_default_config(struct bfp_config *cfg) {
    *cfg=(struct bfp_config){10240,5,100,1000,UINT_MAX,1};
}
struct bfp_cache *bfp_create(unsigned slots, unsigned sectors, int verify, const struct bfp_config *cfg) {
    require(sectors>=1 && sectors<=32,"invalid sectors/page");
    struct bfp_cache *b=allocate(1,sizeof(*b)); bfp_default_config(&b->cfg);
    if (cfg) b->cfg=*cfg;
    require(b->cfg.window && b->cfg.warmup_windows && b->cfg.step_div,"invalid window/warmup/step");
    b->slots=slots; b->sectors=sectors; b->verify=verify; b->next_boundary=b->cfg.window;
    if (slots>=2) {
        b->lo=slots/10; if (!b->lo) b->lo=1;
        unsigned min_r=slots/20; if (!min_r) min_r=1;
        b->hi=slots-min_r;
        b->w=(unsigned)((uint64_t)15*slots/16);
        if (b->w<b->lo) b->w=b->lo;
        if (b->w>b->hi) b->w=b->hi;
        unsigned span=b->hi-b->lo;
        b->hysteresis=slots/300; if (b->hysteresis<8) b->hysteresis=8;
        if (b->hysteresis>span) b->hysteresis=span;
        b->step=slots/b->cfg.step_div; if (b->step<8) b->step=8;
        if (b->step>span) b->step=span;
    } else b->w=slots;
    if (b->cfg.initial_w!=UINT_MAX) {
        require(!b->cfg.adaptive && b->cfg.initial_w<=slots && (!slots || b->cfg.initial_w),"invalid fixed test split");
        b->w=b->cfg.initial_w;
    }
    b->initial_w=b->goal=b->w; b->r=slots-b->w;
    table_init(&b->data,slots); table_init(&b->ghost,slots);
    table_init(&b->read_history,slots); table_init(&b->parity_history,slots);
    return b;
}
unsigned bfp_slots(const struct bfp_cache *b) { return b->slots; }
unsigned bfp_write_limit(const struct bfp_cache *b) { return b->w; }
unsigned bfp_read_limit(const struct bfp_cache *b) { return b->r; }
unsigned bfp_admit_limit(const struct bfp_cache *b) { return b->pending && b->goal<b->w ? b->goal : b->w; }
unsigned bfp_read_count(const struct bfp_cache *b) { return b->data.count; }
unsigned bfp_hit_ns(const struct bfp_cache *b) { return b->cfg.hit_ns; }
int bfp_pending(const struct bfp_cache *b) { return b->pending; }
static void valid_mask(struct bfp_cache *b, unsigned mask) {
    unsigned full=b->sectors==32 ? UINT_MAX : (1u<<b->sectors)-1;
    require(mask && !(mask & ~full),"invalid read mask");
}
int bfp_read_begin(struct bfp_cache *b, unsigned lpn, unsigned mask, uint64_t version,
                   const uint64_t *expected, struct bfp_read_token *t) {
    require(lpn!=0,"invalid data LPN"); valid_mask(b,mask);
    memset(t,0,sizeof(*t)); t->lpn=lpn; t->mask=mask; t->version=version;
    ++b->stats.reads;
    struct node *n=lookup(&b->data,lpn);
    if (n && n->version!=version) { remove_node(&b->data,n); n=NULL; ++b->stats.stale_nodes; }
    if (n && (n->mask & mask)==mask) {
        t->action=BFP_HIT; ++b->stats.hits; touch(&b->data,n);
        if (b->verify) {
            require(expected && n->values,"missing independent read values");
            for (unsigned k=0;k<b->sectors;++k) if (mask & (1u<<k)) {
                require(n->values[k]==expected[k],"read cache payload mismatch"); t->values[k]=n->values[k];
            }
        }
        return 1;
    }
    ++b->stats.misses;
    if (b->verify) {
        require(expected!=NULL,"missing NAND-read snapshot");
        for (unsigned k=0;k<b->sectors;++k) if (mask & (1u<<k)) t->values[k]=expected[k];
    }
    if (n) { ++b->stats.partial_misses; t->action=BFP_MERGE; }
    else {
        ++b->stats.ghost_queries;
        n=lookup(&b->ghost,lpn);
        if (n) { ++b->stats.ghost_hits; touch(&b->ghost,n); t->action=BFP_REPEAT; }
        else t->action=b->data.count<b->r ? BFP_COLD_SPACE : BFP_GHOST_ONLY;
    }
    return 0;
}
void bfp_read_finish(struct bfp_cache *b, struct bfp_read_token *t, unsigned read_mask, uint64_t version) {
    require(!t->consumed,"duplicate read completion/token consumption"); t->consumed=1;
    if (t->action==BFP_HIT) { require(!read_mask && t->version==version,"invalid/stale DRAM response"); return; }
    require(read_mask==t->mask,"read-back mask differs from actual NAND request");
    if (t->action==BFP_COLD_SPACE || t->action==BFP_GHOST_ONLY) history_note(&b->ghost,t->lpn);
    if (t->version!=version) { ++b->stats.stale_drops; return; }
    if (t->action==BFP_GHOST_ONLY) { ++b->stats.ghost_only; return; }
    if (!b->r) { ++b->stats.cold_drops; return; }
    struct node *n=lookup(&b->data,t->lpn);
    if (n && n->version!=version) { remove_node(&b->data,n); n=NULL; ++b->stats.stale_nodes; }
    if (!n && b->data.count>=b->r) {
        if (t->action==BFP_COLD_SPACE) { ++b->stats.cold_drops; return; }
        remove_node(&b->data,b->data.tail); ++b->stats.evictions;
    }
    if (!n) { n=insert(&b->data,t->lpn); n->version=version; if (b->verify) n->values=allocate(32,sizeof(uint64_t)); ++b->stats.fills; }
    else { touch(&b->data,n); ++b->stats.merges; }
    n->mask|=read_mask;
    if (b->verify) for (unsigned k=0;k<b->sectors;++k) if (read_mask & (1u<<k)) n->values[k]=t->values[k];
    history_note(&b->read_history,t->lpn); ++b->stats.read_events; ++b->window_reads;
}
void bfp_invalidate_write(struct bfp_cache *b, unsigned lpn) {
    struct node *n=lookup(&b->data,lpn); if (n) { remove_node(&b->data,n); ++b->stats.invalidations; }
}
void bfp_invalidate_member(struct bfp_cache *b, unsigned member) {
    for (struct node *n=b->data.head,*next;n;n=next) {
        next=n->next; if ((n->key-1)%3==member) remove_node(&b->data,n);
    }
}
void bfp_clear_reads(struct bfp_cache *b) { clear_table(&b->data); }
void bfp_note_parity(struct bfp_cache *b, unsigned stripe) {
    history_note(&b->parity_history,stripe); ++b->stats.parity_events; ++b->window_parity;
}
static int descending(const void *a, const void *b) {
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b; return x<y ? 1 : x>y ? -1 : 0;
}
static uint64_t *prefix(const uint64_t *values, unsigned n) {
    uint64_t *p=allocate((size_t)n+1,sizeof(*p));
    if (n) {
        require(values!=NULL,"missing frequencies"); memcpy(p+1,values,(size_t)n*sizeof(*p));
        qsort(p+1,n,sizeof(*p),descending);
        for (unsigned i=1;i<=n;++i) p[i]=add(p[i],p[i-1]);
    }
    return p;
}
int bfp_choose_target(const uint64_t *w, unsigned wn, const uint64_t *r, unsigned rn,
                      unsigned slots, unsigned current, unsigned *target) {
    require(current<=slots && target,"invalid optimizer capacity");
    uint64_t *wp=prefix(w,wn),*rp=prefix(r,rn);
    *target=current;
    int signal=wp[wn]!=0 || rp[rn]!=0;
    if (signal) {
        uint64_t best=0; int found=0;
        for (uint64_t k=0;k<=(uint64_t)slots;++k) {
            uint64_t sw=wp[k<wn ? (unsigned)k : wn], sr=rp[slots-k<rn ? slots-(unsigned)k : rn];
            require(sw<=UINT64_MAX/10,"weighted profit overflow");
            uint64_t profit=add(10*sw,sr);
            if (!found || profit>best) { best=profit; *target=(unsigned)k; found=1; }
        }
    }
    free(wp); free(rp); return signal;
}
static uint64_t *counts(struct table *t, uint64_t *sum) {
    uint64_t *v=allocate((size_t)t->count+1,sizeof(*v)); unsigned i=0; *sum=0;
    for (struct node *n=t->head;n;n=n->next) { v[i++]=n->count; *sum=add(*sum,n->count); }
    require(i==t->count,"history count mismatch"); return v;
}
int bfp_host_complete(struct bfp_cache *b, uint64_t completed, int64_t now, FILE *log) {
    if (completed<b->next_boundary) return 0;
    require(completed==b->next_boundary,"skipped host completion boundary");
    b->next_boundary=add(b->next_boundary,b->cfg.window); ++b->stats.windows;
    uint64_t sw,sr,*w=counts(&b->parity_history,&sw),*r=counts(&b->read_history,&sr);
    unsigned raw=b->w,bounded=b->w; const char *phase="WARMUP"; int proposed=0;
    int adaptive=b->cfg.adaptive && b->hi>b->lo;
    if (!adaptive) phase="FIXED";
    else if (b->stats.windows>=b->cfg.warmup_windows) {
        ++b->stats.optimizations;
        int signal=bfp_choose_target(w,b->parity_history.count,r,b->read_history.count,b->slots,b->w,&raw);
        phase="NO_SIGNAL";
        if (signal) {
            b->ema=b->ema_valid ? .30*raw+.70*b->ema : raw; b->ema_valid=1;
            bounded=(unsigned)floor(b->ema+.5);
            if (bounded<b->lo) bounded=b->lo;
            if (bounded>b->hi) bounded=b->hi;
            unsigned diff=bounded>b->w ? bounded-b->w : b->w-bounded;
            if (b->pending) { phase="DEFER_PENDING"; ++b->stats.deferred; }
            else if (diff>=b->hysteresis) {
                unsigned step=diff<b->step ? diff : b->step;
                b->goal=bounded>b->w ? b->w+step : b->w-step;
                b->pending=1; ++b->stats.proposals; proposed=1; phase="PROPOSE";
            } else phase="HYSTERESIS";
        }
    }
    if (log) fprintf(log,"bfp_window,%llu,%llu,%lld,%s,%llu,%llu,%llu,%llu,%u,%.6f,%u,%u,%u,%u,%u,%u,0\n",
        (unsigned long long)b->stats.windows,(unsigned long long)completed,(long long)now,phase,
        (unsigned long long)b->window_reads,(unsigned long long)b->window_parity,
        (unsigned long long)sr,(unsigned long long)sw,raw,b->ema_valid ? b->ema : (double)b->w,
        bounded,b->hysteresis,b->step,b->w,b->r,b->pending ? b->goal : b->w);
    for (struct node *n=b->read_history.head;n;n=n->next) n->count=0;
    for (struct node *n=b->parity_history.head;n;n=n->next) n->count=0;
    b->window_reads=b->window_parity=0; free(w); free(r); return proposed;
}
int bfp_advance_resize(struct bfp_cache *b, unsigned parity_count, int64_t now, FILE *log) {
    if (!b->pending || (b->goal<b->w && parity_count>b->goal)) return 0;
    unsigned old=b->w,read_limit=b->slots-b->goal;
    while (b->data.count>read_limit) { remove_node(&b->data,b->data.tail); ++b->stats.resize_evictions; }
    b->w=b->goal; b->r=read_limit; b->pending=0; ++b->stats.transfers;
    unsigned moved=old>b->w ? old-b->w : b->w-old; b->stats.moved=add(b->stats.moved,moved);
    if (log) fprintf(log,"bfp_transfer,%llu,%lld,COMMIT,%u,%u,%u\n",(unsigned long long)b->stats.windows,(long long)now,old,b->w,moved);
    return 1;
}
void bfp_observe(struct bfp_cache *b, unsigned parity_count) {
    require(b->w+b->r==b->slots && b->data.count<=b->r,"read quota invariant");
    unsigned persistent=b->slots ? parity_count : 0;
    require(persistent<=b->w && persistent+b->data.count<=b->slots,"total cache budget exceeded");
    require(b->slots || parity_count<=1,"multiple temporary parity pages");
    if (parity_count && !b->slots) b->stats.scratch_peak=1;
    if (persistent>b->stats.peak_parity) b->stats.peak_parity=persistent;
    if (b->data.count>b->stats.peak_read) b->stats.peak_read=b->data.count;
    if ((uint64_t)persistent+b->data.count>b->stats.peak_total) b->stats.peak_total=(uint64_t)persistent+b->data.count;
}
static void verify_table(struct table *t) {
    unsigned n=0; struct node *prev=NULL;
    for (struct node *p=t->head;p;p=p->next) {
        require(p->prev==prev && lookup(t,p->key)==p,"hash/LRU invariant"); prev=p;
        require(++n<=t->limit,"LRU cycle/capacity invariant");
    }
    require(n==t->count && prev==t->tail,"LRU count/tail invariant");
    unsigned hashed=0;
    for (size_t i=0;i<t->bucket_count;++i) for (struct node *p=t->buckets[i];p;p=p->hash_next)
        require(++hashed<=t->count && bucket(t,p->key)==i,"hash count/bucket invariant");
    require(hashed==t->count,"hash node count invariant");
}
void bfp_verify(struct bfp_cache *b, unsigned parity_count, bfp_node_check check, void *context) {
    bfp_observe(b,parity_count);
    verify_table(&b->data); verify_table(&b->ghost); verify_table(&b->read_history); verify_table(&b->parity_history);
    for (struct node *n=b->data.head;n;n=n->next) {
        valid_mask(b,n->mask);
        require(!check || check(context,n->key,n->version,n->mask,n->values),"resident read version/content invariant");
    }
    require(b->stats.reads==b->stats.hits+b->stats.misses && b->stats.partial_misses<=b->stats.misses,"read accounting invariant");
}
const struct bfp_stats *bfp_get_stats(const struct bfp_cache *b) { return &b->stats; }
void bfp_statistics(const struct bfp_cache *b, FILE *f, unsigned page_bytes, int detail) {
#define K(name, value) fprintf(f,"bfp_" name "=%llu\n",(unsigned long long)(value))
    fprintf(f,"bfp_model=%s\nbfp_read_history_source=admitted_fill\nbfp_policy_cpu_model=metadata_not_charged\nbfp_metadata_in_budget=0\n",BFP_MODEL);
    fprintf(f,"bfp_implementation_revision=%s\n",BFP_IMPLEMENTATION_REVISION);
    K("capacity_pages",b->slots); K("effective_capacity_bytes",(uint64_t)b->slots*page_bytes);
    K("initial_parity_pages",b->initial_w); K("initial_read_pages",b->slots-b->initial_w);
    K("parity_pages",b->w); K("read_pages",b->r); K("read_used_at_end",b->data.count); K("resize_pending",b->pending);
    K("read_peak_pages",b->stats.peak_read); K("parity_peak_pages",b->stats.peak_parity);
    K("persistent_peak_bytes",b->stats.peak_total*page_bytes); K("scratch_peak_bytes",b->stats.scratch_peak*page_bytes);
    K("ghost_limit",b->ghost.limit); K("read_history_limit",b->read_history.limit); K("parity_history_limit",b->parity_history.limit);
    K("window",b->cfg.window); K("warmup_windows",b->cfg.warmup_windows); K("step_div",b->cfg.step_div);
    K("adaptive_enabled",b->cfg.adaptive && b->hi>b->lo);
    fprintf(f,"bfp_alpha=0.30\nbfp_parity_weight=10\nbfp_read_weight=1\n");
    K("min_parity_pages",b->lo); K("min_read_pages",b->slots>=2 ? b->slots-b->hi : 0);
    K("hysteresis",b->hysteresis); K("max_step",b->step); K("hit_ns",b->cfg.hit_ns);
    K("host_read_pages",b->stats.reads); K("read_hits",b->stats.hits); K("read_misses",b->stats.misses); K("partial_misses",b->stats.partial_misses);
    fprintf(f,"bfp_read_cache_hit_ratio=%.6f\n",b->stats.reads ? (double)b->stats.hits / (double)b->stats.reads : 0.0);
    K("read_fills",b->stats.fills); K("read_merges",b->stats.merges); K("ghost_queries",b->stats.ghost_queries); K("ghost_hits",b->stats.ghost_hits);
    K("ghost_only",b->stats.ghost_only); K("cold_fill_drops",b->stats.cold_drops); K("stale_fill_drops",b->stats.stale_drops);
    K("write_invalidations",b->stats.invalidations); K("read_evictions",b->stats.evictions); K("resize_read_evictions",b->stats.resize_evictions);
    K("read_history_events",b->stats.read_events); K("parity_history_events",b->stats.parity_events);
    K("windows",b->stats.windows); K("optimizations",b->stats.optimizations); K("resize_proposals",b->stats.proposals);
    K("resize_transfers",b->stats.transfers); K("moved_pages",b->stats.moved); K("deferred_windows",b->stats.deferred);
    fprintf(f,"bfp_cache_check=PASS\nbfp_payload_check=%s\n",b->verify ? "PASS" : "disabled");
    if (detail) { K("ghost_nodes_at_end",b->ghost.count); K("read_history_nodes",b->read_history.count); K("parity_history_nodes",b->parity_history.count); }
#undef K
}
void bfp_destroy(struct bfp_cache *b) {
    if (!b) return;
    struct table *tables[]={&b->data,&b->ghost,&b->read_history,&b->parity_history};
    for (unsigned i=0;i<4;++i) { clear_table(tables[i]); free(tables[i]->buckets); }
    free(b);
}
