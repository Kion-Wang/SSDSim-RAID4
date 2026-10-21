#include "raid4.h"
#include "bfp_cache.h"
#include "experiment_stats.h"
#include <assert.h>
#include <limits.h>

#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

/* One prefix per process. Keep the complete trace basename, including extension. */
static char result_prefix[1536];
static int request_logging;
static int detailed_statistics;
int raid4_detailed_statistics(void) { return detailed_statistics; }
/* Legacy backend writers require a valid stream, but their diagnostic output
 * is not part of the experiment result. The OS null device consumes no disk. */
FILE *raid4_discard_open(void) {
#ifdef _WIN32
    return fopen("NUL", "w");
#else
    return fopen("/dev/null", "w");
#endif
}
FILE *raid4_result_open(const char *suffix, const char *mode) {
    char path[2048];
    int n = snprintf(path, sizeof(path), "%s%s", result_prefix, suffix);
    if (n < 0 || (size_t)n >= sizeof(path)) { errno = ENAMETOOLONG; return NULL; }
    FILE *f = fopen(path, mode);
    if (!f) perror(path);
    return f;
}
int raid4_prepare_outputs(struct ssd_info *ssd, int rounds, const struct parity_options *options) {
    const char *base = ssd->tracefilename;
    for (const char *p = base; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    if (!*base) { fprintf(stderr, "Trace path must name a file.\n"); return 0; }
    FILE *trace = fopen(ssd->tracefilename, "r");
    if (!trace) { perror(ssd->tracefilename); return 0; }
    fclose(trace);
#ifdef _WIN32
    int rc = _mkdir("result");
#else
    int rc = mkdir("result", 0777);
#endif
    if (rc != 0 && errno != EEXIST) { perror("result"); return 0; }
    char directory[64];
    snprintf(directory, sizeof(directory), "result/%s", parity_scheme_name(options->scheme));
#ifdef _WIN32
    rc = _mkdir(directory);
#else
    rc = mkdir(directory, 0777);
#endif
    if (rc != 0 && errno != EEXIST) { perror(directory); return 0; }
    int n = snprintf(result_prefix, sizeof(result_prefix), "%s/%sx%d", directory, base, rounds);
    if (n < 0 || (size_t)n >= sizeof(result_prefix)) return 0;
    if (options->recovery_member >= 0) {
        size_t used = strlen(result_prefix);
        snprintf(result_prefix + used, sizeof(result_prefix) - used, "_recover%d", options->recovery_member);
        if (options->recover_after >= 0) {
            used = strlen(result_prefix);
            snprintf(result_prefix + used, sizeof(result_prefix) - used, "_after%lld", (long long)options->recover_after);
        }
    }
    if (options->alternating_data) {
        size_t used=strlen(result_prefix);
        snprintf(result_prefix+used,sizeof(result_prefix)-used,"_alternating");
    }
    request_logging = getenv("RAID4_REQUEST_LOG") != NULL;
    const char *detail = getenv("RAID4_DETAILED_STATS");
    detailed_statistics = detail && strcmp(detail,"1")==0;
    snprintf(ssd->outputfilename, sizeof(ssd->outputfilename), "%s.out", result_prefix);
    snprintf(ssd->statisticfilename, sizeof(ssd->statisticfilename), "%s.dat", result_prefix);
    printf("Result file: %s\n", ssd->statisticfilename);
    return 1;
}

/* Requests in one stripe are serialized. Different stripes retain NAND parallelism.
 * Baseline caches full parity; PPC caches the XOR of its covered page members.
 * PPC baseline versions stay protected until the full parity program completes.
 * Only idle lines can be evicted; a line stays pinned until its NAND write completes.
 * The legacy SSDSim stores metadata, not bytes; XOR_process models computation time.
 */
enum { JOB_NEW, JOB_READS, JOB_XOR, JOB_WRITES, JOB_COMMIT, JOB_PATCH_ADMIT, JOB_PATCH_DIFF, JOB_GC_RETRY, JOB_CACHE_READ };
enum { FLUSH_NONE, FLUSH_READS, FLUSH_XOR, FLUSH_WRITES };
/* Optional synthetic 64-bit sector payloads: reference data, baseline versions,
 * PPC contents and NAND parity are independent values, checked through XOR. */
struct r4_proof {
    uint64_t current[3][32], base[3][32], partial[32], disk[32], commit[32];
};
/* Simulator backing data, not simulated cache storage: one deterministic
 * 64-bit word represents each 512-byte sector. Only nonzero delta sectors
 * consume PATCH's modeled cache budget; metadata is excluded for all schemes. */
struct r4_patch {
    uint64_t data[3][32], disk[32], delta[32];
    unsigned mask, reserved, disk_state;
};
struct r4_job {
    struct request io;
    struct request *host;
    struct r4_job *next;
    unsigned lpn, state, operation, stage;
    int rebuild;
    int64_t ready;
    uint64_t generation;
    uint64_t patch_delta[32];
    unsigned old_state, xor_units;
    struct bfp_read_token *bfp_read;
    int bfp_write_started, bfp_parity_observed;
};
struct r4_row {
    unsigned id, parity_state;
    int cached, dirty, flushing, active;
    unsigned members, ppc_state, baseline_state[3];
    int64_t old_ppn[3], flush_ready;
    int flush_stage, gc_requested, force_current;
    struct ppc_commit_plan plan;
    struct r4_proof *proof;
    struct r4_patch *patch;
    struct r4_job *head, *tail;
    struct request flush_io;
    struct experiment_request *flush_stats; /* Borrowed until this flush completes. */
    struct r4_row *next_active, *lru_prev, *lru_next;
    struct r4_row *admit_next;
    uint64_t admit_demand, reclaim_credit;
    int admit_queued, replacement_flush;
    uint64_t bfp_version[3];
};
/* Counters belong to the phase in which the physical operation starts.
 * Foreground work is quiesced at recovery boundaries, so operations cannot
 * leak between the normal and recovery phases. GC reads/programs are explicit. */
struct r4_backend_stats {
    uint64_t reads, programs, read_sectors, write_transfer_sectors, erases, moved;
    uint64_t page_type[3], wait_total[2], wait_count[2], wait_max[2];
    uint64_t channel_reads[16], channel_programs[16], channel_erases[16];
    uint64_t channel_read_wait[16], channel_write_wait[16], chip_erases[16][2];
};
struct raid4_controller {
    struct r4_row **rows, *active, *lru_head, *lru_tail;
    unsigned cache_limit, cache_count;
    unsigned long long submitted, completed, read_sectors, write_sectors;
    unsigned long long data_writes[16], parity_writes[16], parity_reads;
    unsigned long long cache_hits, cache_misses, evictions, xor_jobs;
    uint64_t xor_sector_ops[3], xor_service_ns[3];
    uint64_t controller_progress;
    unsigned long long recovered, recovery_reads, pre_data, pre_parity;
    int recovering;
    struct parity_options options;
    unsigned long long update_first, update_new_member, update_repeat, update_reads, rmw_reads;
    unsigned long long commit_count[COMMIT_REASON_COUNT], commit_reads, commit_old, commit_current;
    unsigned long long protected_pages, peak_protected, gc_waits, checks, recovery_partial, recovery_old;
    uint64_t gc_write_yields;
    unsigned long long recovery_uncommitted, recovery_injections;
    int64_t recovery_elapsed;
    struct r4_backend_stats backend[2]; /* 0: normal, 1: recovery */
    uint64_t *plane_erases;
    uint64_t init_reads, init_programs, init_free_pages, init_used_pages;
    uint64_t min_lsn, max_lsn, max_read_latency, max_write_latency;
    uint64_t period_reads, period_writes, period_read_ns, period_write_ns;
    uint64_t period_gc, period_moved;
    int64_t normal_start, normal_recovery_pause;
    uint64_t patch_limit, patch_used, patch_peak, patch_persistent_peak;
    uint64_t patch_created, patch_merged, patch_cancelled, patch_unchanged;
    uint64_t patch_updates, patch_reads, patch_commit_reads, patch_sync, patch_scratch_peak;
    uint64_t patch_peak_entries;
    struct r4_row *admit_head, *admit_tail;
    uint64_t admit_demand, admit_count, reclaim_pending_units;
    uint64_t flushing_count, replacement_pending, replacement_pending_peak, victim_scan_steps;
    uint64_t patch_recovery_delta;
    struct r4_row *patch_scratch;
    FILE *events;
    struct bfp_cache *bfp;
    struct experiment_stats *experiment;
};
static void periodic_statistics(struct ssd_info *s, int final);
static int is_patch(const struct ssd_info *s) { return s->raid4 && s->raid4->options.scheme==PARITY_PATCH; }
static int full_parity(const struct ssd_info *s) {
    return s->raid4->options.scheme==PARITY_BASELINE || s->raid4->options.scheme==PARITY_BFP;
}
static unsigned parity_limit(struct ssd_info *s) {
    struct raid4_controller *c=s->raid4;
    return c->bfp && bfp_slots(c->bfp) ? bfp_admit_limit(c->bfp) : c->cache_limit;
}
static int resize_pending(struct ssd_info *s) { return s->raid4->bfp && bfp_pending(s->raid4->bfp); }
static FILE *bfp_detail(struct ssd_info *s) { return detailed_statistics ? s->statisticfile : NULL; }
static void fail(const char *message) {
    fprintf(stderr, "RAID4: %s\n", message);
    exit(EXIT_FAILURE);
}
static unsigned full_state(struct ssd_info *s) {
    return s->parameter->subpage_page == 32 ? UINT32_MAX : (1u << s->parameter->subpage_page) - 1u;
}
enum { R4_XOR_UPDATE, R4_XOR_COMMIT, R4_XOR_RECOVERY };
/* One unit combines two present 512-byte operands. The first operand is a
 * copy; absent operands are known zero. Metadata/copy time is excluded equally
 * for all schemes. Masks describe available operands, not payload magnitudes. */
static unsigned xor_merge(unsigned *support, unsigned source) {
    unsigned units=size(*support & source);
    *support|=source;
    return units;
}
static unsigned update_xor_units(struct ssd_info *s, struct r4_row *r, struct r4_job *j) {
    unsigned support=raid4_is_ppc(s) ? r->ppc_state : r->parity_state;
    if (raid4_is_ppc(s) && !(r->members & (1u<<((j->lpn-1)%3))))
        return xor_merge(&support,j->old_state | j->state);
    /* Remove the old contribution and add the replacement. Unchanged sectors
     * cancel algebraically and need not be processed in a repeated update. */
    support&=j->state;
    unsigned units=xor_merge(&support,j->old_state & j->state);
    return units+xor_merge(&support,j->state);
}
static int64_t charge_xor(struct ssd_info *s, unsigned units, unsigned phase) {
    if (!units) return s->current_time;
    struct raid4_controller *c=s->raid4;
    c->xor_sector_ops[phase]+=units;
    c->xor_service_ns[phase]+=XOR_process(s,units);
    ++c->xor_jobs;
    return s->cpu_next_state_predict_time;
}
static struct page_info *physical(struct ssd_info *s, const struct local *l) {
    return &s->channel_head[l->channel].chip_head[l->chip].die_head[l->die].plane_head[l->plane].blk_head[l->block].page_head[l->page];
}
static unsigned channel_for(unsigned stripe, unsigned member) { return member * 4 + (stripe - 1) % 4; }
int raid4_is_ppc(const struct ssd_info *s) {
    return s->raid4 && s->raid4->options.scheme == PARITY_PPC;
}
void raid4_note_service(struct ssd_info *s, struct sub_request *q) {
    if (!s->raid4) return;
    struct r4_backend_stats *b = &s->raid4->backend[s->raid4->recovering != 0];
    unsigned ch = q->location->channel, op = q->operation;
    uint64_t wait = (uint64_t)(s->current_time - q->begin_time);
    experiment_service(s->raid4->experiment,s->raid4->recovering,op,wait,
                       q->req ? q->req->experiment_stats : NULL);
    b->wait_total[op] += wait; ++b->wait_count[op];
    if (wait > b->wait_max[op]) b->wait_max[op] = wait;
    if (op == READ) {
        ++b->reads; b->read_sectors += q->size; ++b->channel_reads[ch];
        b->channel_read_wait[ch] += wait;
    } else {
        ++b->programs; b->write_transfer_sectors += q->size; ++b->channel_programs[ch];
        ++b->page_type[q->location->page % 3]; b->channel_write_wait[ch] += wait;
    }
}
static void check(struct ssd_info *s, int condition, const char *message) {
    if (!condition) fail(message);
    ++s->raid4->checks;
}
static uint64_t payload(unsigned lpn, unsigned sector, uint64_t generation) {
    uint64_t x = generation * UINT64_C(0x9e3779b97f4a7c15) ^ ((uint64_t)lpn << 6) ^ sector;
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static uint64_t write_payload(struct ssd_info *s, unsigned lpn, unsigned sector, uint64_t generation) {
    return payload(lpn,sector,s->raid4->options.alternating_data ? generation%2 : generation);
}
static uint64_t patch_expected(struct r4_row *r, unsigned k) {
    return r->patch->data[0][k]^r->patch->data[1][k]^r->patch->data[2][k];
}
static void patch_check(struct ssd_info *s, struct r4_row *r) {
    unsigned mask=0;
    for (unsigned k=0;k<s->parameter->subpage_page;++k) {
        if (r->patch->delta[k]) mask|=1u<<k;
        check(s,(r->patch->disk[k]^r->patch->delta[k])==patch_expected(r,k),"PATCH delta/base parity mismatch");
    }
    check(s,mask==r->patch->mask,"PATCH support mask mismatch");
}
static uint64_t expected_parity(struct r4_row *r, unsigned sector) {
    return r->proof->current[0][sector] ^ r->proof->current[1][sector] ^ r->proof->current[2][sector];
}
static void release_versions(struct ssd_info *s, struct r4_row *r) {
    for (unsigned i = 0; i < 3; ++i) {
        if (r->old_ppn[i] >= 0) {
            check(s, s->raid4->protected_pages > 0, "protected-page counter underflow");
            --s->raid4->protected_pages;
        }
        r->old_ppn[i] = -1;
        s->dram->map->map_entry[s->trip2Page[r->id].lpn[i]].oldppa = -1;
        r->baseline_state[i] = 0;
    }
    r->members = r->ppc_state = 0; r->force_current = 0;
}
void raid4_preserve_old(struct ssd_info *s, unsigned lpn, unsigned ppn, unsigned state) {
    if (!raid4_is_ppc(s) || lpn == s->stripe.checkLpn || s->raid4->recovering) return;
    struct r4_row *r = s->raid4->rows[(lpn - 1) / 3 + 1];
    unsigned slot = (lpn - 1) % 3;
    check(s, r && r->cached && r->head, "data overwrite without a pinned PPC entry");
    if (!(r->members & (1u << slot)) && r->old_ppn[slot] < 0) {
        check(s, r->baseline_state[slot] == state, "PPC baseline version changed before first overwrite");
        r->old_ppn[slot] = ppn;
        s->dram->map->map_entry[lpn].oldppa = ppn;
        if (++s->raid4->protected_pages > s->raid4->peak_protected)
            s->raid4->peak_protected = s->raid4->protected_pages;
    }
}
static int done(struct ssd_info *s, struct sub_request *q) {
    return q->current_state == SR_COMPLETE || (q->next_state == SR_COMPLETE && q->next_state_predict_time <= s->current_time);
}
static int io_done(struct ssd_info *s, struct request *io) {
    for (struct sub_request *q = io->subs; q; q = q->next_subs) if (!done(s, q)) return 0;
    return 1;
}
static void release_sub(struct sub_request *q) {
    while (q) {
        struct sub_request *next = q->update;
        free(q->completeCountR); free(q->completeCountW); free(q->location); free(q);
        q = next;
    }
}
static void release_io(struct request *io) {
    struct sub_request *q = io->subs;
    while (q) { struct sub_request *next = q->next_subs; release_sub(q); q = next; }
    memset(io, 0, sizeof(*io));
}
static struct r4_row *row_for(struct ssd_info *s, unsigned id) {
    struct raid4_controller *c = s->raid4;
    if (!id || id >= s->stripe.allStripe) fail("stripe index out of range");
    if (!c->rows[id]) {
        struct r4_row *r = calloc(1, sizeof(*r)); alloc_assert(r, "raid4 row");
        r->id = id;
        for (unsigned i = 0; i < 3; ++i) r->old_ppn[i] = -1;
        if (c->options.verify_data) { r->proof = calloc(1, sizeof(*r->proof)); alloc_assert(r->proof, "parity reference payloads"); }
        if (is_patch(s)) { r->patch=calloc(1,sizeof(*r->patch)); alloc_assert(r->patch,"PATCH synthetic backing data"); }
        struct RaidInfo *info = &s->trip2Page[id];
        info->using = 1; info->Pchannel = channel_for(id, 3);
        for (unsigned i = 0; i < 3; ++i) {
            unsigned lpn = (id - 1) * 3 + i + 1;
            info->lpn[i] = lpn;
            s->page2Trip[lpn] = id;
        }
        c->rows[id] = r;
    }
    return c->rows[id];
}
static void activate(struct raid4_controller *c, struct r4_row *r) {
    if (r->active) return;
    ++c->controller_progress;
    r->next_active = c->active; c->active = r; r->active = 1;
}
static void lru_remove(struct raid4_controller *c, struct r4_row *r) {
    if (r->lru_prev) r->lru_prev->lru_next = r->lru_next; else c->lru_head = r->lru_next;
    if (r->lru_next) r->lru_next->lru_prev = r->lru_prev; else c->lru_tail = r->lru_prev;
    r->lru_prev = r->lru_next = NULL;
}
static void lru_touch(struct raid4_controller *c, struct r4_row *r) {
    if (r->cached) lru_remove(c, r);
    else { r->cached = 1; ++c->cache_count; }
    r->lru_next = c->lru_head;
    if (c->lru_head) c->lru_head->lru_prev = r; else c->lru_tail = r;
    c->lru_head = r;
}
static void issue(struct ssd_info *s, struct r4_row *r, struct request *io, unsigned lpn, unsigned mask, unsigned op) {
    if (!mask) return;
    io->time = s->current_time;
    /* Sync flush ownership survives release_io() between its read/write phases.
     * Autonomous parity/GC/rebuild I/O has no host owner. */
    io->experiment_stats=io==&r->flush_io ? r->flush_stats :
        r->head && r->head->host ? r->head->host->experiment_stats : NULL;
    struct sub_request *q = creat_sub_request(s, lpn, size(mask), mask, io, op, TARGET_LSB, r->id);
    if (!q) fail("NAND request allocation failed");
    if (op==WRITE) {
        uint64_t all=0;
        for (unsigned ch=0;ch<s->parameter->channel_number;++ch) all+=s->chanSubRLenNow[ch];
        experiment_write_enqueue(s->raid4->experiment,s->raid4->recovering,
                                 s->chanSubRLenNow[q->location->channel],all);
    }
    unsigned member = lpn == s->stripe.checkLpn ? 3 : (lpn - 1) % 3;
    if (q->location->channel / 4 != member) fail("request routed to the wrong RAID4 member");
    if (op == WRITE) {
        if (member == 3) ++s->raid4->parity_writes[q->location->channel];
        else ++s->raid4->data_writes[q->location->channel];
    } else {
        if (member == 3) ++s->raid4->parity_reads;
        if (s->raid4->recovering) ++s->raid4->recovery_reads;
    }
    if (s->raid4->events) fprintf(s->raid4->events, "%lld %u %s %u %08x %u\n", (long long)s->current_time, r->id,
            op == READ ? "READ" : "WRITE", lpn, mask, q->location->channel);
}
/* Addressed reads bypass the newest-LPN map. In particular, an old PPC version
 * is invalid to the FTL but remains physically readable until its commit ends. */
struct sub_request *raid4_read_physical(struct ssd_info *s, struct request *io,
    unsigned stripe, unsigned lpn, unsigned ppn, unsigned mask) {
    struct sub_request *q = calloc(1, sizeof(*q)); alloc_assert(q, "physical read");
    q->location = find_location(s, ppn);
    q->lpn = lpn; q->ppn = ppn; q->state = mask; q->size = size(mask);
    q->operation = READ; q->raidNUM = stripe; q->req = io; q->flagUser = 1;
    q->begin_time = s->current_time; q->current_time = MAX_INT64;
    q->current_state = SR_WAIT; q->next_state = SR_R_C_A_TRANSFER; q->next_state_predict_time = MAX_INT64;
    q->completeCountR = calloc(s->parameter->channel_number, sizeof(unsigned long long));
    q->completeCountW = calloc(s->parameter->channel_number, sizeof(unsigned long long));
    alloc_assert(q->completeCountR, "read counters"); alloc_assert(q->completeCountW, "write counters");
    for (unsigned i = 0; i < s->parameter->channel_number; ++i) {
        q->completeCountR[i] = s->channel_head[i].completeCountR;
        q->completeCountW[i] = s->channel_head[i].completeCountW;
    }
    q->next_subs = io->subs; io->subs = q;
    struct channel_info *ch = &s->channel_head[q->location->channel];
    if (ch->subs_r_tail) ch->subs_r_tail->next_node = q; else ch->subs_r_head = q;
    ch->subs_r_tail = q;
    ++s->chanSubRLenNow[q->location->channel]; ++s->chanSubCount[q->location->channel];
    s->chanSubLenAll[q->location->channel] += s->chanSubRLenNow[q->location->channel];
    ++s->chanRs[q->location->channel]; ++s->read2;
    if (s->raid4->recovering) ++s->raid4->recovery_reads;
    if (s->raid4->events) fprintf(s->raid4->events, "%lld %u READ %u %08x %u %u OLD\n",
        (long long)s->current_time, stripe, lpn, mask, q->location->channel, ppn);
    return q;
}
static void issue_old(struct ssd_info *s, struct r4_row *r, struct request *io, unsigned slot, unsigned mask) {
    if (!mask) return;
    check(s, r->old_ppn[slot] >= 0, "missing protected baseline page");
    unsigned lpn = s->trip2Page[r->id].lpn[slot];
    struct local *loc = find_location(s, (unsigned)r->old_ppn[slot]);
    check(s, physical(s, loc)->lpn == lpn && physical(s, loc)->free_state == 0,
          "protected baseline page was erased or reused");
    free(loc);
    raid4_read_physical(s, io, r->id, lpn, (unsigned)r->old_ppn[slot], mask);
}
static void start_flush(struct ssd_info *s, struct r4_row *r, enum parity_commit_reason reason) {
    if (r->flushing || !r->dirty) return;
    if (r->head && r->head->stage != JOB_NEW && r->head->stage != JOB_COMMIT &&
        !(is_patch(s) && r->head->stage==JOB_PATCH_ADMIT) &&
        !(raid4_is_ppc(s) && r->head->stage==JOB_GC_RETRY)) fail("attempt to evict a pinned parity line");
    r->flush_stats=NULL;
    if (reason==COMMIT_SYNC) {
        if (!r->head || r->head->stage!=JOB_COMMIT || !r->head->host ||
            r->head->rebuild || r->head->host->operation!=WRITE || !r->head->host->experiment_stats)
            fail("synchronous parity commit lacks its waiting host write");
        r->flush_stats=r->head->host->experiment_stats;
    }
    /* Set before issue_old(), which bypasses issue() for protected versions. */
    r->flush_io.experiment_stats=r->flush_stats;
    r->flushing = 1;
    ++s->raid4->controller_progress;
    r->reclaim_credit=is_patch(s) ? r->patch->reserved : 1;
    s->raid4->reclaim_pending_units+=r->reclaim_credit;
    ++s->raid4->flushing_count;
    r->replacement_flush=reason==COMMIT_REPLACEMENT;
    if (r->replacement_flush) {
        ++s->raid4->replacement_pending;
        if (s->raid4->replacement_pending>s->raid4->replacement_pending_peak)
            s->raid4->replacement_pending_peak=s->raid4->replacement_pending;
    }
    ++s->raid4->commit_count[reason];
    if (is_patch(s)) {
        memset(&r->plan,0,sizeof(r->plan));
        r->plan.use_old=1;
        r->plan.parity_mask=r->patch->disk_state;
        r->plan.read_pages=r->plan.parity_mask!=0;
        if (r->plan.parity_mask) {
            issue(s,r,&r->flush_io,s->stripe.checkLpn,r->plan.parity_mask,READ);
            ++s->raid4->patch_commit_reads;
        }
        r->flush_stage=FLUSH_READS;
    } else if (raid4_is_ppc(s)) {
        unsigned current[3];
        for (unsigned i = 0; i < 3; ++i) current[i] = s->dram->map->map_entry[s->trip2Page[r->id].lpn[i]].state;
        r->plan = ppc_plan_commit(r->members, current, r->baseline_state,
                                 s->trip2Page[r->id].parity_state, r->force_current);
        if (r->plan.use_old) ++s->raid4->commit_old; else ++s->raid4->commit_current;
        s->raid4->commit_reads += r->plan.read_pages;
        for (unsigned i = 0; i < 3; ++i) {
            if (r->plan.use_old) issue_old(s, r, &r->flush_io, i, r->plan.read_mask[i]);
            else issue(s, r, &r->flush_io, s->trip2Page[r->id].lpn[i], r->plan.read_mask[i], READ);
        }
        issue(s, r, &r->flush_io, s->stripe.checkLpn, r->plan.parity_mask, READ);
        r->flush_stage = FLUSH_READS;
    } else {
        issue(s, r, &r->flush_io, s->stripe.checkLpn, r->parity_state, WRITE);
        r->flush_stage = FLUSH_WRITES;
    }
    activate(s->raid4, r);
}
/* Queue only admissions that need additional storage. The head consumes
 * available space first; all waiters may schedule useful parallel victims.
 * Global demand and pending-free credits prevent repeated polls from flushing
 * the entire cache, without fragmenting free space among waiting requests. */
static void admission_request(struct ssd_info *s, struct r4_row *r, uint64_t demand) {
    struct raid4_controller *c=s->raid4;
    if (!r->admit_queued) {
        r->admit_queued=1; r->admit_next=NULL; ++c->admit_count;
        if (c->admit_tail) c->admit_tail->admit_next=r; else c->admit_head=r;
        c->admit_tail=r;
    }
    c->admit_demand-=r->admit_demand; c->admit_demand+=demand;
    r->admit_demand=demand;
}
static void admission_done(struct ssd_info *s, struct r4_row *r) {
    if (!r->admit_queued) return;
    struct raid4_controller *c=s->raid4;
    struct r4_row *prev=NULL, *node=c->admit_head;
    while (node && node!=r) { prev=node; node=node->admit_next; }
    check(s,node==r && c->admit_demand>=r->admit_demand && c->admit_count,"invalid cache admission queue");
    if (prev) prev->admit_next=r->admit_next; else c->admit_head=r->admit_next;
    if (c->admit_tail==r) c->admit_tail=prev;
    c->admit_demand-=r->admit_demand; --c->admit_count;
    ++c->controller_progress;
    r->admit_demand=0; r->admit_queued=0; r->admit_next=NULL;
}
static int needs_reclaim(struct ssd_info *s, uint64_t free_units) {
    struct raid4_controller *c=s->raid4;
    uint64_t debt=c->bfp && c->cache_count>parity_limit(s) ? c->cache_count-parity_limit(s) : 0;
    return c->admit_demand+debt>free_units+c->reclaim_pending_units && c->flushing_count<c->cache_count;
}
static enum parity_commit_reason reclaim_reason(struct ssd_info *s) {
    struct raid4_controller *c=s->raid4;
    uint64_t debt=c->bfp && c->cache_count>parity_limit(s) ? c->cache_count-parity_limit(s) : 0;
    return debt>c->reclaim_pending_units ? COMMIT_RESIZE : COMMIT_REPLACEMENT;
}
static void patch_drop(struct ssd_info *s, struct r4_row *r) {
    struct raid4_controller *c=s->raid4;
    if (r->cached || r->patch->reserved) ++c->controller_progress;
    check(s,c->patch_used>=r->patch->reserved,"PATCH capacity underflow");
    c->patch_used-=r->patch->reserved; r->patch->reserved=0;
    if (c->patch_scratch==r) c->patch_scratch=NULL;
    if (r->cached) { lru_remove(c,r); r->cached=0; --c->cache_count; }
}
static unsigned patch_preview(struct ssd_info *s, struct r4_row *r, struct r4_job *j) {
    unsigned mask=0;
    for (unsigned k=0;k<s->parameter->subpage_page;++k)
        if (r->patch->delta[k]^j->patch_delta[k]) mask|=1u<<k;
    return mask;
}
static int patch_admit(struct ssd_info *s, struct r4_row *r, struct r4_job *j) {
    struct raid4_controller *c=s->raid4;
    unsigned projected=size(patch_preview(s,r,j));
    /* The old delta remains authoritative until the data program completes.
     * A predicted cancellation must not lend its space to another stripe yet. */
    unsigned need=projected>r->patch->reserved ? projected : r->patch->reserved;
    if (c->patch_scratch && c->patch_scratch!=r) return 0;
    uint64_t other=c->patch_used-r->patch->reserved;
    int scratch=need>c->patch_limit;
    uint64_t demand=(scratch ? c->patch_limit : need)-r->patch->reserved;
    int enough=scratch ? !other : other+need<=c->patch_limit;
    if (r->admit_queued || (demand && (!enough || c->admit_head))) admission_request(s,r,demand);
    int turn=!r->admit_queued || !demand || c->admit_head==r;
    if (!turn || (scratch && other) || (!scratch && other+need>c->patch_limit)) {
        uint64_t free_units=c->patch_used<c->patch_limit ? c->patch_limit-c->patch_used : 0;
        if (!needs_reclaim(s,free_units)) return 0;
        struct r4_row *victim=c->lru_tail;
        while (victim && (victim==r || victim->flushing || !victim->patch->reserved ||
            (victim->head && victim->head->stage!=JOB_NEW && victim->head->stage!=JOB_PATCH_ADMIT))) { ++c->victim_scan_steps; victim=victim->lru_prev; }
        if (!victim) return 0;
        ++c->evictions;
        if (victim->dirty) start_flush(s,victim,COMMIT_REPLACEMENT);
        else patch_drop(s,victim);
        return 0;
    }
    admission_done(s,r);
    if (scratch) {
        c->patch_scratch=r;
        c->patch_scratch_peak=s->parameter->page_capacity;
    }
    if (r->cached) ++c->cache_hits; else ++c->cache_misses;
    lru_touch(c,r);
    if (c->cache_count>c->patch_peak_entries) c->patch_peak_entries=c->cache_count;
    c->patch_used=other+need; r->patch->reserved=need;
    check(s,r->patch->reserved>=(unsigned)size(r->patch->mask),"PATCH released authoritative delta before data program");
    check(s,c->patch_scratch ? c->patch_used==c->patch_scratch->patch->reserved && c->patch_used<=s->parameter->subpage_page
                            : c->patch_used<=c->patch_limit,"PATCH cache budget exceeded");
    if (c->patch_used>c->patch_peak) c->patch_peak=c->patch_used;
    if (!scratch && c->patch_used>c->patch_persistent_peak) c->patch_persistent_peak=c->patch_used;
    unsigned delta_mask=0;
    for (unsigned k=0;k<s->parameter->subpage_page;++k)
        if (j->patch_delta[k]) delta_mask|=1u<<k;
    j->xor_units=size(delta_mask & r->patch->mask);
    return 1;
}
static void patch_prepare(struct ssd_info *s, struct r4_row *r, struct r4_job *j) {
    unsigned slot=(j->lpn-1)%3;
    for (unsigned k=0;k<s->parameter->subpage_page;++k)
        j->patch_delta[k]=(j->state & (1u<<k)) ? r->patch->data[slot][k]^write_payload(s,j->lpn,k,j->generation) : 0;
}
static void patch_update(struct ssd_info *s, struct r4_row *r, struct r4_job *j) {
    struct raid4_controller *c=s->raid4;
    unsigned slot=(j->lpn-1)%3, mask=0;
    ++c->patch_updates;
    for (unsigned k=0;k<s->parameter->subpage_page;++k) {
        uint64_t old=r->patch->delta[k];
        if (j->state & (1u<<k)) {
            if (!j->patch_delta[k]) ++c->patch_unchanged;
            r->patch->data[slot][k]=write_payload(s,j->lpn,k,j->generation);
        }
        r->patch->delta[k]^=j->patch_delta[k];
        uint64_t value=r->patch->delta[k];
        if (!old && value) ++c->patch_created;
        if (old && j->patch_delta[k] && value) ++c->patch_merged;
        if (old && !value) ++c->patch_cancelled;
        if (value) mask|=1u<<k;
    }
    r->patch->mask=mask;
    unsigned actual=size(mask);
    check(s,r->patch->reserved>=actual,"PATCH reservation/support mismatch");
    check(s,c->patch_used>=r->patch->reserved-actual,"PATCH release underflow");
    c->patch_used-=r->patch->reserved-actual; r->patch->reserved=actual;
    r->parity_state|=j->state;
    r->dirty=mask || r->parity_state!=r->patch->disk_state;
    patch_check(s,r);
    if (!r->dirty) patch_drop(s,r);
}
static int acquire_cache(struct ssd_info *s, struct r4_row *r) {
    struct raid4_controller *c = s->raid4;
    if (r->flushing) return 0;
    if (r->cached) { admission_done(s,r); ++c->cache_hits; lru_touch(c, r); return 1; }
    unsigned limit=parity_limit(s);
    uint64_t free_units=c->cache_count<limit ? limit-c->cache_count : 0;
    if (!free_units || c->admit_head || r->admit_queued) admission_request(s,r,1);
    if ((r->admit_queued && c->admit_head!=r) || !free_units) {
        if (!needs_reclaim(s,free_units)) return 0;
        struct r4_row *victim = c->lru_tail;
        while (victim && (victim->flushing || (victim->head && victim->head->stage != JOB_NEW))) { ++c->victim_scan_steps; victim=victim->lru_prev; }
        if (!victim) return 0;
        ++c->evictions;
        if (victim->dirty) { start_flush(s, victim, reclaim_reason(s)); return 0; }
        lru_remove(c, victim); victim->cached = 0; --c->cache_count;
        ++c->controller_progress;
        return 0;
    }
    admission_done(s,r);
    ++c->cache_misses; lru_touch(c, r);
    if (raid4_is_ppc(s)) {
        for (unsigned i = 0; i < 3; ++i) r->baseline_state[i] = s->dram->map->map_entry[s->trip2Page[r->id].lpn[i]].state;
        if (r->proof) { memcpy(r->proof->base, r->proof->current, sizeof(r->proof->base)); memset(r->proof->partial, 0, sizeof(r->proof->partial)); }
    } else if (r->proof) memcpy(r->proof->partial, r->proof->disk, sizeof(r->proof->partial));
    return 1;
}
static void enqueue(struct ssd_info *s, struct r4_row *r, struct r4_job *j) {
    if (r->tail) r->tail->next = j; else r->head = j;
    r->tail = j; activate(s->raid4, r);
}
static void finish_job(struct ssd_info *s, struct r4_row *r) {
    ++s->raid4->controller_progress;
    struct r4_job *j = r->head;
    release_io(&j->io);
    if (j->host) {
        ++j->host->now;
        j->host->response_time = s->current_time;
    }
    if (j->rebuild) ++s->raid4->recovered;
    r->head = j->next; if (!r->head) r->tail = NULL;
    free(j->bfp_read); free(j);
}
static void proof_update(struct ssd_info *s, struct r4_row *r, struct r4_job *j) {
    if (!r->proof) return;
    unsigned slot = (j->lpn - 1) % 3;
    for (unsigned k = 0; k < s->parameter->subpage_page; ++k) {
        uint64_t old = r->proof->current[slot][k];
        uint64_t value = (j->state & (1u << k)) ? write_payload(s,j->lpn, k, j->generation) : old;
        if (!raid4_is_ppc(s) || (r->members & (1u << slot))) r->proof->partial[k] ^= old;
        r->proof->partial[k] ^= value;
        r->proof->current[slot][k] = value;
    }
    if (raid4_is_ppc(s)) {
        unsigned members = r->members | (1u << slot);
        for (unsigned k = 0; k < s->parameter->subpage_page; ++k) {
            uint64_t value = 0;
            for (unsigned i = 0; i < 3; ++i) if (members & (1u << i)) value ^= r->proof->current[i][k];
            check(s, value == r->proof->partial[k], "partial parity payload mismatch");
        }
    } else for (unsigned k = 0; k < s->parameter->subpage_page; ++k) {
        uint64_t value=r->proof->partial[k];
        if (is_patch(s)) {
            check(s,value==r->patch->delta[k],"PATCH independent delta mismatch");
            value^=r->proof->disk[k];
        }
        check(s, expected_parity(r, k) == value, "cached parity payload mismatch");
    }
}
static void poll_flush(struct ssd_info *s, struct r4_row *r) {
    struct raid4_controller *c = s->raid4;
    if (!r->flushing) return;
    int previous_stage=r->flush_stage;
    if (r->flush_stage == FLUSH_READS && io_done(s, &r->flush_io)) {
        release_io(&r->flush_io);
        if (r->proof) for (unsigned k = 0; k < s->parameter->subpage_page; ++k) {
            uint64_t value = r->proof->partial[k];
            if (r->plan.use_old) value ^= r->proof->disk[k];
            for (unsigned i = 0; i < 3; ++i) if (r->plan.read_mask[i])
                value ^= r->plan.use_old ? r->proof->base[i][k] : r->proof->current[i][k];
            check(s, value == expected_parity(r, k), "committed parity payload mismatch");
            r->proof->commit[k] = value;
        }
        r->flush_ready = s->current_time;
        unsigned units=0;
        if (is_patch(s)) units=size(r->patch->mask & r->patch->disk_state);
        else {
            unsigned support=r->ppc_state;
            units+=xor_merge(&support,r->plan.parity_mask);
            for (unsigned i=0;i<3;++i) units+=xor_merge(&support,r->plan.read_mask[i]);
        }
        if (units) {
            r->flush_ready=charge_xor(s,units,R4_XOR_COMMIT);
        }
        r->flush_stage = FLUSH_XOR;
    }
    if (r->flush_stage == FLUSH_XOR && r->flush_ready <= s->current_time) {
        issue(s, r, &r->flush_io, s->stripe.checkLpn, r->parity_state, WRITE);
        r->flush_stage = FLUSH_WRITES;
    }
    if (r->flush_stage == FLUSH_WRITES && io_done(s, &r->flush_io)) {
        release_io(&r->flush_io);
        r->flush_stats=NULL;
        if (r->proof) memcpy(r->proof->disk, full_parity(s) ? r->proof->partial : r->proof->commit, sizeof(r->proof->disk));
        if (is_patch(s)) {
            for (unsigned k=0;k<s->parameter->subpage_page;++k) {
                r->patch->disk[k]^=r->patch->delta[k]; r->patch->delta[k]=0;
            }
            r->patch->mask=0; r->patch->disk_state=r->parity_state;
            if (r->proof) memset(r->proof->partial,0,sizeof(r->proof->partial));
            patch_check(s,r); patch_drop(s,r);
        }
        release_versions(s, r);
        check(s,c->reclaim_pending_units>=r->reclaim_credit && c->flushing_count,"cache pending-free credit underflow");
        c->reclaim_pending_units-=r->reclaim_credit; r->reclaim_credit=0; --c->flushing_count;
        if (r->replacement_flush) { --c->replacement_pending; r->replacement_flush=0; }
        r->flushing = r->dirty = r->gc_requested = 0; r->flush_stage = FLUSH_NONE;
        if (r->cached) { lru_remove(c, r); r->cached = 0; --c->cache_count; }
    }
    if (r->flush_stage!=previous_stage) ++c->controller_progress;
}
static void rebuild_reads(struct ssd_info *s, struct r4_row *r, struct r4_job *j) {
    struct raid4_controller *c = s->raid4;
    int parity = j->lpn == s->stripe.checkLpn;
    unsigned target = parity ? 3 : (j->lpn - 1) % 3;
    if (is_patch(s)) {
        unsigned support=0;
        if (!parity && r->patch->mask) ++c->patch_recovery_delta;
        if (!parity) {
            unsigned mask=r->patch->disk_state & j->state;
            issue(s,r,&j->io,s->stripe.checkLpn,mask,READ);
            j->xor_units+=xor_merge(&support,mask);
            j->xor_units+=xor_merge(&support,r->patch->mask & j->state);
        }
        for (unsigned i=0;i<3;++i) if (i!=target) {
            unsigned mask=s->dram->map->map_entry[s->trip2Page[r->id].lpn[i]].state & j->state;
            issue(s,r,&j->io,s->trip2Page[r->id].lpn[i],mask,READ);
            j->xor_units+=xor_merge(&support,mask);
        }
        for (unsigned k=0;k<s->parameter->subpage_page;++k) {
            uint64_t value=parity ? 0 : r->patch->disk[k]^r->patch->delta[k];
            for (unsigned i=0;i<3;++i) if (i!=target) {
                value^=r->patch->data[i][k];
            }
            check(s,value==(parity ? patch_expected(r,k) : r->patch->data[target][k]),"PATCH recovery value mismatch");
            if (r->proof) {
                uint64_t ref=parity ? 0 : r->proof->disk[k]^r->proof->partial[k];
                for (unsigned i=0;i<3;++i) if (i!=target) ref^=r->proof->current[i][k];
                check(s,ref==(parity ? expected_parity(r,k) : r->proof->current[target][k]),"PATCH independent recovery mismatch");
            }
        }
        return;
    }
    int partial = raid4_is_ppc(s) && r->cached && r->dirty;
    int use_partial = partial && (parity || (r->members & (1u << target)));
    unsigned support=use_partial ? r->ppc_state & j->state : 0;
    if (use_partial) ++c->recovery_partial; else ++c->recovery_old;
    for (unsigned i = 0; i < 3; ++i) {
        if (i == target) continue;
        unsigned lpn = s->trip2Page[r->id].lpn[i];
        unsigned mask=0;
        if (parity) {
            if (!partial || !(r->members & (1u << i))) mask=s->dram->map->map_entry[lpn].state;
        } else if (use_partial) {
            if (r->members & (1u << i)) mask=s->dram->map->map_entry[lpn].state;
        } else mask=partial && (r->members & (1u << i)) ? r->baseline_state[i] : s->dram->map->map_entry[lpn].state;
        mask&=j->state;
        if (!use_partial && partial && (r->members & (1u<<i))) issue_old(s,r,&j->io,i,mask);
        else issue(s,r,&j->io,lpn,mask,READ);
        j->xor_units+=xor_merge(&support,mask);
    }
    if (!parity && !use_partial) {
        unsigned mask=s->trip2Page[r->id].parity_state & j->state;
        issue(s,r,&j->io,s->stripe.checkLpn,mask,READ);
        j->xor_units+=xor_merge(&support,mask);
    }
    if (r->proof) for (unsigned k = 0; k < s->parameter->subpage_page; ++k) {
        uint64_t value = use_partial ? r->proof->partial[k] : (parity ? 0 : r->proof->disk[k]);
        for (unsigned i = 0; i < 3; ++i) {
            if (i == target) continue;
            if (parity) { if (!partial || !(r->members & (1u << i))) value ^= r->proof->current[i][k]; }
            else if (use_partial) { if (r->members & (1u << i)) value ^= r->proof->current[i][k]; }
            else value ^= partial && (r->members & (1u << i)) ? r->proof->base[i][k] : r->proof->current[i][k];
        }
        check(s, value == (parity ? expected_parity(r,k) : r->proof->current[target][k]), "failure recovery payload mismatch");
    }
}
/* A queued, unallocated data program can wait for the very GC commit it pins.
 * Keep its RMW data and request, but remove it from NAND scheduling while the
 * preceding parity epoch commits. No mapping or payload has changed in SR_WAIT.
 * After reacquiring an empty PPC line, this update contributes a copied page;
 * earlier reads/XOR remain charged, and the NAND write is issued only once. */
static void yield_gc_write(struct ssd_info *s, struct r4_row *r) {
    struct r4_job *j=r->head;
    if (!raid4_is_ppc(s) || !j || j->rebuild || j->stage!=JOB_WRITES) return;
    struct sub_request *q=j->io.subs;
    if (!q || q->next_subs || q->update || q->operation!=WRITE || q->current_state!=SR_WAIT) return;
    struct chip_info *chip=&s->channel_head[q->location->channel].chip_head[q->location->chip];
    for (unsigned die=0;die<s->parameter->die_chip;++die)
        for (unsigned plane=0;plane<s->parameter->plane_die;++plane)
            if (chip->die_head[die].plane_head[plane].free_page) return;
    delete_w_sub_request(s,q->location->channel,q);
    q->next_node=NULL;
    j->stage=JOB_GC_RETRY;
    ++s->raid4->gc_write_yields; ++s->raid4->controller_progress;
}
static void poll_bfp_resize(struct ssd_info *s) {
    struct raid4_controller *c=s->raid4;
    if (!c->bfp) return;
    if (bfp_advance_resize(c->bfp,c->cache_count,s->current_time,bfp_detail(s))) {
        experiment_borrow_commit(c->experiment,c->completed,s->current_time,
                                 bfp_write_limit(c->bfp),bfp_read_limit(c->bfp));
        c->cache_limit=bfp_write_limit(c->bfp);
        if (!c->cache_limit) c->cache_limit=1;
        ++c->controller_progress;
    }
    bfp_observe(c->bfp,c->cache_count);
    if (!bfp_pending(c->bfp)) return;
    unsigned limit=parity_limit(s);
    uint64_t free_units=c->cache_count<limit ? limit-c->cache_count : 0;
    if (!needs_reclaim(s,free_units)) return;
    struct r4_row *victim=c->lru_tail;
    while (victim && (victim->flushing || (victim->head && victim->head->stage!=JOB_NEW))) {
        ++c->victim_scan_steps; victim=victim->lru_prev;
    }
    if (!victim) return;
    ++c->evictions;
    if (victim->dirty) start_flush(s,victim,reclaim_reason(s));
    else {
        lru_remove(c,victim); victim->cached=0; --c->cache_count; ++c->controller_progress;
    }
}
static void poll_controller(struct ssd_info *s) {
    struct raid4_controller *c = s->raid4;
    poll_bfp_resize(s);
    if (s->cpu_next_state_predict_time <= s->current_time) s->cpu_current_state = CPU_IDLE;
    struct r4_row **link = &c->active;
    while (*link) {
        struct r4_row *r = *link;
        int previous_stage=r->head ? (int)r->head->stage : -1;
        if (r->gc_requested && r->dirty && !r->flushing) {
            yield_gc_write(s,r);
            if (!r->head || r->head->stage==JOB_NEW || r->head->stage==JOB_GC_RETRY ||
                (is_patch(s) && r->head->stage==JOB_PATCH_ADMIT)) start_flush(s,r,COMMIT_GC);
        }
        poll_flush(s, r);
        struct r4_job *j = r->head;
        if (j && !r->flushing) {
            if (j->stage==JOB_GC_RETRY && acquire_cache(s,r)) {
                struct sub_request *q=j->io.subs;
                struct channel_info *ch=&s->channel_head[q->location->channel];
                if (ch->subs_w_tail) ch->subs_w_tail->next_node=q; else ch->subs_w_head=q;
                ch->subs_w_tail=q;
                j->stage=JOB_WRITES;
            }
            if (j->stage == JOB_COMMIT) { finish_job(s, r); j = NULL; }
            if (j && j->stage == JOB_NEW) {
                if (j->rebuild) { rebuild_reads(s, r, j); j->stage = JOB_READS; }
                else if (j->operation == READ) {
                    if ((s->dram->map->map_entry[j->lpn].state & j->state) != j->state) fail("read includes an uninitialized sector");
                    int hit=0;
                    if (c->bfp) {
                        j->bfp_read=calloc(1,sizeof(*j->bfp_read)); alloc_assert(j->bfp_read,"BFP read token");
                        unsigned slot=(j->lpn-1)%3;
                        hit=bfp_read_begin(c->bfp,j->lpn,j->state,r->bfp_version[slot],
                            r->proof ? r->proof->current[slot] : NULL,j->bfp_read);
                        if (hit) { j->ready=s->current_time+bfp_hit_ns(c->bfp); j->stage=JOB_CACHE_READ; }
                    }
                    experiment_read_query(c->experiment,hit);
                    if (!hit) { issue(s, r, &j->io, j->lpn, j->state, READ); j->stage = JOB_READS; }
                } else if (is_patch(s)) {
                    j->old_state=s->dram->map->map_entry[j->lpn].state;
                    issue(s,r,&j->io,j->lpn,j->old_state,READ);
                    if (j->old_state) ++c->patch_reads;
                    if (j->old_state & ~j->state) ++c->rmw_reads;
                    j->stage=JOB_READS;
                } else {
                    int was_cached = r->cached;
                    if (acquire_cache(s, r)) {
                        experiment_parity_access(c->experiment,r->id);
                        if (c->bfp) {
                            unsigned slot=(j->lpn-1)%3;
                            if (!j->bfp_write_started) {
                                bfp_invalidate_write(c->bfp,j->lpn);
                                check(s,r->bfp_version[slot]!=UINT64_MAX,"BFP logical version overflow");
                                ++r->bfp_version[slot]; j->bfp_write_started=1;
                            }
                            if (!j->bfp_parity_observed) { bfp_note_parity(c->bfp,r->id); j->bfp_parity_observed=1; }
                            bfp_observe(c->bfp,c->cache_count);
                        }
                        unsigned old = j->old_state = s->dram->map->map_entry[j->lpn].state;
                        unsigned read_mask = old;
                        if (raid4_is_ppc(s)) {
                            unsigned bit = 1u << ((j->lpn - 1) % 3);
                            if (!(r->members & bit)) read_mask = old & ~j->state;
                            if ((r->members & bit) && read_mask) ++c->update_reads;
                            if ((old & ~j->state) != 0) ++c->rmw_reads;
                        }
                        issue(s, r, &j->io, j->lpn, read_mask, READ);
                        if (!raid4_is_ppc(s) && !was_cached && s->trip2Page[r->id].location)
                            issue(s, r, &j->io, s->stripe.checkLpn, s->trip2Page[r->id].parity_state, READ);
                        j->stage = JOB_READS;
                    }
                }
            }
            if (j && j->stage==JOB_CACHE_READ && j->ready<=s->current_time) {
                bfp_read_finish(c->bfp,j->bfp_read,0,r->bfp_version[(j->lpn-1)%3]);
                finish_job(s,r); j=NULL;
            }
            if (j && j->stage == JOB_READS && io_done(s, &j->io)) {
                release_io(&j->io);
                if (j->operation == READ && !j->rebuild) {
                    if (c->bfp) {
                        bfp_read_finish(c->bfp,j->bfp_read,j->state,r->bfp_version[(j->lpn-1)%3]);
                        bfp_observe(c->bfp,c->cache_count);
                    }
                    finish_job(s, r); j = NULL;
                }
                else if (is_patch(s) && !j->rebuild) {
                    patch_prepare(s,r,j);
                    j->ready=charge_xor(s,size(j->state & j->old_state),R4_XOR_UPDATE);
                    j->stage=JOB_PATCH_DIFF;
                } else {
                    j->ready = s->current_time;
                    unsigned units=j->rebuild ? j->xor_units : update_xor_units(s,r,j);
                    j->ready=charge_xor(s,units,j->rebuild ? R4_XOR_RECOVERY : R4_XOR_UPDATE);
                    j->stage = JOB_XOR;
                }
            }
            if (j && j->stage==JOB_PATCH_DIFF && j->ready<=s->current_time)
                j->stage=JOB_PATCH_ADMIT;
            if (j && j->stage==JOB_PATCH_ADMIT && patch_admit(s,r,j)) {
                j->ready=s->current_time;
                if (j->xor_units) {
                    j->ready=charge_xor(s,j->xor_units,R4_XOR_UPDATE);
                }
                j->stage=JOB_XOR;
            }
            if (j && j->stage == JOB_XOR && j->ready <= s->current_time) {
                unsigned state = j->state;
                if (j->lpn != s->stripe.checkLpn) state |= s->dram->map->map_entry[j->lpn].state;
                issue(s, r, &j->io, j->lpn, state, WRITE); j->stage = JOB_WRITES;
            }
            if (j && j->stage == JOB_WRITES && io_done(s, &j->io)) {
                if (!j->rebuild) {
                    if (raid4_is_ppc(s)) {
                        unsigned bit=1u<<((j->lpn-1)%3);
                        if (!r->members) ++c->update_first;
                        else if (r->members & bit) ++c->update_repeat;
                        else ++c->update_new_member;
                    }
                    if (is_patch(s)) patch_update(s,r,j);
                    proof_update(s, r, j);
                    if (!is_patch(s)) {
                        r->members |= 1u << ((j->lpn - 1) % 3);
                        if (raid4_is_ppc(s)) r->ppc_state|=j->old_state | j->state;
                        r->parity_state |= j->state; r->dirty = 1;
                    }
                } else if (j->lpn == s->stripe.checkLpn) {
                    if (r->proof) for (unsigned k=0;k<s->parameter->subpage_page;++k) r->proof->disk[k] = expected_parity(r,k);
                    release_versions(s,r); r->dirty = 0;
                    if (is_patch(s)) {
                        for (unsigned k=0;k<s->parameter->subpage_page;++k) {
                            r->patch->disk[k]=patch_expected(r,k); r->patch->delta[k]=0;
                        }
                        r->patch->mask=0; r->patch->disk_state=r->parity_state;
                        if (r->proof) memset(r->proof->partial,0,sizeof(r->proof->partial));
                        patch_check(s,r); patch_drop(s,r);
                    } else if (r->cached) { lru_remove(c,r); r->cached=0; --c->cache_count; }
                }
                int sync=is_patch(s) ? c->patch_scratch==r && r->dirty : s->parameter->dram_capacity < s->parameter->page_capacity;
                if (!j->rebuild && sync) {
                    if (is_patch(s)) ++c->patch_sync;
                    release_io(&j->io); j->stage=JOB_COMMIT; start_flush(s,r,COMMIT_SYNC);
                } else {
                    finish_job(s, r);
                }
            }
        }
        if (r->head && (int)r->head->stage!=previous_stage) ++c->controller_progress;
        if (!r->head && !r->flushing && !r->gc_requested) { *link = r->next_active; r->active = 0; r->next_active = NULL; }
        else link = &r->next_active;
    }
    poll_bfp_resize(s);
}
static void complete_hosts(struct ssd_info *s) {
    struct request **link = &s->request_queue;
    while (*link) {
        struct request *r = *link;
        if (r->now != r->all) { link = &r->next_node; continue; }
        int64_t latency = r->response_time - r->time;
        if (latency < 0) fail("negative host latency");
        if (request_logging) fprintf(s->outputfile, "%lld %u %u %u %lld %lld %lld\n", (long long)r->time, r->lsn, r->size, r->operation,
                (long long)r->time, (long long)r->response_time, (long long)latency);
        if (r->operation == READ) { ++s->read_request_count; s->read_avg += latency; }
        else { ++s->write_request_count; s->write_avg += latency; }
        uint64_t *maximum = r->operation == READ ? &s->raid4->max_read_latency : &s->raid4->max_write_latency;
        if ((uint64_t)latency > *maximum) *maximum = latency;
        ++s->completed_request_count; ++s->raid4->completed;
        if (s->raid4->bfp) {
            struct raid4_controller *c=s->raid4;
            uint64_t previous=bfp_get_stats(c->bfp)->windows;
            int proposed=bfp_host_complete(c->bfp,c->completed,s->current_time,bfp_detail(s));
            if (bfp_get_stats(c->bfp)->windows!=previous)
                experiment_borrow_window(c->experiment,c->completed,bfp_get_stats(c->bfp)->windows,
                                         s->current_time,bfp_write_limit(c->bfp),bfp_read_limit(c->bfp),proposed);
            if (proposed) ++c->controller_progress;
        }
        if (s->raid4->completed % 10000 == 0) periodic_statistics(s, 0);
        --s->request_queue_length;
        experiment_host_complete(s->raid4->experiment,r->experiment_stats,r->operation,r->time,r->response_time);
        *link = r->next_node; free(r);
    }
    s->request_tail = s->request_queue;
    while (s->request_tail && s->request_tail->next_node) s->request_tail = s->request_tail->next_node;
}
static int64_t next_event(struct ssd_info *s) {
    int64_t next = MAX_INT64;
#define CONSIDER(t) do { int64_t event_ = (t); if (event_ > s->current_time && event_ < next) next = event_; } while (0)
    for (unsigned ch = 0; ch < s->parameter->channel_number; ++ch) {
        CONSIDER(s->channel_head[ch].next_state_predict_time);
        for (unsigned chip = 0; chip < s->parameter->chip_channel[ch]; ++chip)
            CONSIDER(s->channel_head[ch].chip_head[chip].next_state_predict_time);
    }
    for (struct r4_row *r = s->raid4->active; r; r = r->next_active) {
        if (r->head && (r->head->stage == JOB_XOR || r->head->stage == JOB_PATCH_DIFF || r->head->stage==JOB_CACHE_READ)) CONSIDER(r->head->ready);
        if (r->flushing && r->flush_stage == FLUSH_XOR) CONSIDER(r->flush_ready);
        struct request *io = r->flushing ? &r->flush_io : r->head ? &r->head->io : NULL;
        if (io) for (struct sub_request *q = io->subs; q; q = q->next_subs) CONSIDER(q->next_state_predict_time);
    }
#undef CONSIDER
    return next;
}
static void pump(struct ssd_info *s) {
    /* NAND first removes completed reads from its queues; then owners may release them. */
    uint64_t before;
    do {
        before=s->raid4->controller_progress;
        process(s); poll_controller(s); complete_hosts(s);
        process(s);
    } while (s->raid4->controller_progress!=before);
}
static void advance(struct ssd_info *s) {
    int64_t next = next_event(s);
    if (next == MAX_INT64) {
        /* A predecessor may just have completed; give newly exposed jobs one turn. */
        pump(s); next = next_event(s);
        if (!s->raid4->active && !s->request_queue && !resize_pending(s)) return;
        if (next == MAX_INT64) fail("pending requests have no runnable NAND/CPU event");
    }
    s->current_time = next;
}
/* Quiesce submitted work and GC without flushing unrelated dirty cache lines.
 * Stale GC nodes can be removed without creating a hardware event; consume them
 * until the queue shrinks or real NAND/CPU work becomes runnable. */
static void settle_foreground(struct ssd_info *s) {
    for (;;) {
        unsigned before=s->gc_request;
        pump(s);
        int64_t next=next_event(s);
        if (s->raid4->active || s->request_queue || resize_pending(s)) { advance(s); continue; }
        if (next!=MAX_INT64) { s->current_time=next; continue; }
        if (!s->gc_request) break;
        if (s->gc_request<before) continue;
        fail("GC has pending work but no runnable event or reclaimable victim");
    }
}
static void drain(struct ssd_info *s) {
    settle_foreground(s);
    /* Dirty parities at EOF are committed independently across stripes. */
    struct r4_row *line=s->raid4->lru_tail;
    while (line) {
        struct r4_row *previous=line->lru_prev;
        if (line->dirty) start_flush(s,line,COMMIT_EOF);
        else if (is_patch(s)) patch_drop(s,line);
        else { lru_remove(s->raid4,line); line->cached=0; --s->raid4->cache_count; }
        line=previous;
    }
    settle_foreground(s);
}
static int in_block(const struct local *l, unsigned ch, unsigned chip, unsigned die, unsigned plane, unsigned block) {
    return l->channel==ch && l->chip==chip && l->die==die && l->plane==plane && l->block==block;
}
void raid4_check_erase(struct ssd_info *s, unsigned ch, unsigned chip, unsigned die, unsigned plane, unsigned block) {
    if (s->raid4) {
        unsigned phase = s->raid4->recovering != 0;
        struct r4_backend_stats *b = &s->raid4->backend[phase];
        ++b->erases; ++b->channel_erases[ch]; ++b->chip_erases[ch][chip];
        size_t planes = (size_t)s->parameter->channel_number * 2 * s->parameter->die_chip * s->parameter->plane_die;
        size_t index = (((size_t)ch * 2 + chip) * s->parameter->die_chip + die) * s->parameter->plane_die + plane;
        ++s->raid4->plane_erases[phase * planes + index];
    }
    if (!raid4_is_ppc(s)) return;
    for (struct r4_row *r=s->raid4->lru_head;r;r=r->lru_next) for (unsigned i=0;i<3;++i) {
        if (r->old_ppn[i]<0) continue;
        struct local *loc=find_location(s,(unsigned)r->old_ppn[i]);
        int protected=in_block(loc,ch,chip,die,plane,block); free(loc);
        check(s,!protected,"attempt to erase a protected PPC baseline page");
    }
}
/* Both schemes share ordinary GC, NAND geometry, migration and timing.
 * A victim waits without occupying a channel while its stripe commits run.
 * This is ordinary greedy GC, not CA-PPC/PC-aware victim selection. */
int raid4_gc_for_channel(struct ssd_info *s, unsigned ch) {
    struct raid4_controller *c=s->raid4;
    struct gc_operation *g=s->channel_head[ch].gc_command;
    for (;g;g=g->next_node) {
        struct chip_info *ci=&s->channel_head[ch].chip_head[g->chip];
        if (ci->current_state==CHIP_IDLE || (ci->next_state==CHIP_IDLE && ci->next_state_predict_time<=s->current_time)) break;
    }
    if (!g) return FAILURE;
    struct plane_info *pl=&s->channel_head[ch].chip_head[g->chip].die_head[g->die].plane_head[g->plane];
    unsigned block=g->block;
    if (block==UINT_MAX) {
        /* With zero free pages, a fully invalid victim can still be erased
         * without migration scratch space. */
        int has_active=find_active_block(s,ch,g->chip,g->die,g->plane)==SUCCESS;
        unsigned best=0;
        for (unsigned b=0;b<s->parameter->block_plane;++b) {
            struct blk_info *bl=&pl->blk_head[b];
            unsigned valid=s->parameter->page_block-bl->free_page_num-bl->invalid_page_num;
            /* With no writable block, active_block is only a stale index.
             * A full invalid block at that index needs no migration space. */
            if (bl->free_page_num || (has_active && b==pl->active_block) || valid>pl->free_page) continue;
            if (bl->invalid_page_num>0 && (unsigned)bl->invalid_page_num>=best) { best=bl->invalid_page_num; block=b; }
        }
        if (block==UINT_MAX) {
            if (!pl->free_page) fail("GC has no scratch space or fully invalid victim; increase overprovisioning or GC threshold");
            delete_gc_node(s,ch,g); return FAILURE;
        }
        g->block=block;
    }
    int wait=0;
    for (struct r4_row *r=c->lru_head;r;r=r->lru_next) for (unsigned i=0;i<3;++i) {
        if (r->old_ppn[i]<0) continue;
        struct local *loc=find_location(s,(unsigned)r->old_ppn[i]);
        int found=in_block(loc,ch,g->chip,g->die,g->plane,block); free(loc);
        if (found) {
            if (!r->gc_requested) ++c->controller_progress;
            r->gc_requested=1; activate(c,r); wait=1;
        }
    }
    /* Reads already addressed to a physical page must complete before GC moves it. */
    for (struct sub_request *q=s->channel_head[ch].subs_r_head;q;q=q->next_node)
        if (!done(s,q) && in_block(q->location,ch,g->chip,g->die,g->plane,block)) wait=1;
    if (wait) { ++c->gc_waits; return FAILURE; }
    unsigned transferred=0,moved=0;
    for (unsigned page=0;page<s->parameter->page_block;++page) {
        if (!pl->blk_head[block].page_head[page].valid_state) continue;
        struct local loc={0};
        loc.channel=ch; loc.chip=g->chip; loc.die=g->die; loc.plane=g->plane; loc.block=block; loc.page=page;
        unsigned sectors = size(pl->blk_head[block].page_head[page].valid_state);
        uint64_t before_types[3] = {s->write_lsb_count, s->write_csb_count, s->write_msb_count};
        if (move_page(s,&loc,&transferred)!=SUCCESS) fail("PPC GC page migration failed");
        struct r4_backend_stats *b = &c->backend[c->recovering != 0];
        ++b->reads; ++b->programs; ++b->moved;
        ++b->channel_reads[ch]; ++b->channel_programs[ch];
        b->read_sectors += sectors; b->write_transfer_sectors += sectors;
        b->page_type[0] += s->write_lsb_count - before_types[0];
        b->page_type[1] += s->write_csb_count - before_types[1];
        b->page_type[2] += s->write_msb_count - before_types[2];
        ++moved; ++s->page_move_count;
    }
    if (erase_operation(s,ch,g->chip,g->die,g->plane,block)!=SUCCESS) fail("PPC GC erase failed");
    ++s->normal_erase;
    struct channel_info *channel=&s->channel_head[ch];
    struct chip_info *ci=&channel->chip_head[g->chip];
    channel->current_state=CHANNEL_GC; channel->current_time=s->current_time; channel->next_state=CHANNEL_IDLE;
    uint64_t duration=(uint64_t)moved*(14ULL*s->parameter->time_characteristics.tWC+s->parameter->time_characteristics.tR+s->parameter->time_characteristics.tPROG)
        +(uint64_t)transferred*SECTOR*(s->parameter->time_characteristics.tWC+s->parameter->time_characteristics.tRC);
    channel->next_state_predict_time=s->current_time+duration;
    ci->current_state=CHIP_ERASE_BUSY; ci->current_time=s->current_time; ci->next_state=CHIP_IDLE;
    ci->next_state_predict_time=channel->next_state_predict_time+s->parameter->time_characteristics.tBERS;
    delete_gc_node(s,ch,g); return SUCCESS;
}
struct trace_record { int64_t time; unsigned lsn, count, operation; };
static int read_record(FILE *f, struct trace_record *r) {
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line; while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\n' || *p == '\r' || *p == '#' || !*p) continue;
        long long time; unsigned long long lsn; int device, count, operation;
        if (sscanf(p, "%lld %d %llu %d %d", &time, &device, &lsn, &count, &operation) != 5 || time < 0 || count <= 0 || lsn > UINT_MAX || (operation != READ && operation != WRITE)) fail("malformed trace record");
        r->time = time; r->lsn = (unsigned)lsn; r->count = count; r->operation = operation;
        return 1;
    }
    return 0;
}
static void submit(struct ssd_info *s, const struct trace_record *record, int64_t arrival) {
    struct request *host = calloc(1, sizeof(*host)); alloc_assert(host, "host request");
    host->time = arrival; host->lsn = record->lsn; host->size = record->count; host->operation = record->operation;
    host->experiment_stats=experiment_request_create(host->operation);
    if (s->request_tail) s->request_tail->next_node = host; else s->request_queue = host;
    s->request_tail = host; ++s->request_queue_length; ++s->raid4->submitted;
    uint64_t last_lsn = (uint64_t)record->lsn + record->count - 1;
    if (record->lsn < s->raid4->min_lsn) s->raid4->min_lsn = record->lsn;
    if (last_lsn > s->raid4->max_lsn) s->raid4->max_lsn = last_lsn;
    if (host->operation == READ) s->raid4->read_sectors += host->size; else s->raid4->write_sectors += host->size;
    uint64_t capacity = (uint64_t)s->stripe.checkStart * s->parameter->subpage_page;
    uint64_t cursor = host->lsn % capacity; unsigned remaining = host->size;
    while (remaining) {
        unsigned offset = cursor % s->parameter->subpage_page;
        unsigned count = s->parameter->subpage_page - offset; if (count > remaining) count = remaining;
        unsigned lpn = (unsigned)(cursor / s->parameter->subpage_page) + 1;
        struct r4_row *row = row_for(s, (lpn - 1) / 3 + 1);
        struct r4_job *j = calloc(1, sizeof(*j)); alloc_assert(j, "RAID4 job");
        j->generation = s->raid4->submitted;
        j->lpn = lpn; j->state = (unsigned)set_entry_state(s, offset, count); j->operation = host->operation; j->host = host;
        ++host->all; enqueue(s, row, j);
        remaining -= count; cursor = (cursor + count) % capacity;
    }
}
static void prefill(struct ssd_info *s, unsigned stripe, unsigned lpn, unsigned state) {
    unsigned member = lpn == s->stripe.checkLpn ? 3 : (lpn - 1) % 3;
    s->stripe.nowStripe = stripe;
    s->ssdToken = member; s->channelToken = (stripe - 1) % 4;
    s->chipToken = ((stripe - 1) / 4) % s->parameter->chip_channel[0];
    raid_pre_read(s, lpn * s->parameter->subpage_page, lpn, s->parameter->subpage_page, full_state(s));
    struct local *loc = lpn == s->stripe.checkLpn ? s->trip2Page[stripe].location : find_location(s, s->dram->map->map_entry[lpn].pn);
    physical(s, loc)->valid_state = state; physical(s, loc)->free_state = full_state(s) & ~state;
    if (member == 3) s->trip2Page[stripe].parity_state = state;
    else { s->dram->map->map_entry[lpn].state = state; free(loc); }
}
static void age_invalid_page(struct ssd_info *s) {
    unsigned ch=s->ssdToken*4+s->channelToken, chip=s->chipToken;
    unsigned die=s->channel_head[ch].chip_head[chip].token;
    unsigned plane=s->channel_head[ch].chip_head[chip].die_head[die].token;
    if (!s->channel_head[ch].chip_head[chip].die_head[die].plane_head[plane].free_page) {
        /* Legacy preprocessing returns PPN 0 on a full selected plane. Aging
         * must find real free space instead of accidentally invalidating PPN 0. */
        int found=0;
        for (ch=0;ch<s->parameter->channel_number && !found;++ch)
        for (chip=0;chip<s->parameter->chip_channel[ch] && !found;++chip)
        for (die=0;die<s->parameter->die_chip && !found;++die)
        for (plane=0;plane<s->parameter->plane_die;++plane) {
            if (!s->channel_head[ch].chip_head[chip].die_head[die].plane_head[plane].free_page) continue;
            s->ssdToken=ch/4; s->channelToken=ch%4; s->chipToken=chip;
            s->channel_head[ch].chip_head[chip].token=die;
            s->channel_head[ch].chip_head[chip].die_head[die].token=plane;
            found=1; break;
        }
        if (!found) fail("aging exhausted physical free pages");
    }
    raid_pre_invalid(s);
}
static void preprocess(struct ssd_info *s) {
    FILE *f = fopen(s->tracefilename, "r"); if (!f) fail("cannot open trace");
    struct trace_record record;
    uint64_t capacity = (uint64_t)s->stripe.checkStart * s->parameter->subpage_page;
    while (read_record(f, &record)) {
        ++s->total_request_num;
        if (record.operation != READ) continue;
        uint64_t cursor = record.lsn % capacity; unsigned remaining = record.count;
        while (remaining) {
            unsigned offset = cursor % s->parameter->subpage_page;
            unsigned count = s->parameter->subpage_page - offset; if (count > remaining) count = remaining;
            unsigned lpn = (unsigned)(cursor / s->parameter->subpage_page) + 1;
            row_for(s, (lpn - 1) / 3 + 1);
            s->dram->map->map_entry[lpn].state |= (unsigned)set_entry_state(s, offset, count);
            remaining -= count; cursor = (cursor + count) % capacity;
        }
    }
    fclose(f);
    for (unsigned id = 1; id < s->stripe.allStripe; ++id) {
        struct r4_row *r = s->raid4->rows[id]; if (!r) continue;
        for (unsigned slot = 0; slot < 3; ++slot) {
            unsigned lpn = s->trip2Page[id].lpn[slot], state = s->dram->map->map_entry[lpn].state;
            if (state) { prefill(s, id, lpn, state); r->parity_state |= state; ++s->raid4->pre_data; }
        }
        if (r->parity_state) { prefill(s, id, s->stripe.checkLpn, r->parity_state); ++s->raid4->pre_parity; }
    }
    if (s->raid4->options.verify_data) for (unsigned id=1;id<s->stripe.allStripe;++id) {
        struct r4_row *r=s->raid4->rows[id]; if (!r) continue;
        for (unsigned i=0;i<3;++i) {
            unsigned lpn=s->trip2Page[id].lpn[i], state=s->dram->map->map_entry[lpn].state;
            for (unsigned k=0;k<s->parameter->subpage_page;++k)
                if (state & (1u<<k)) r->proof->current[i][k]=payload(lpn,k,0);
        }
        for (unsigned k=0;k<s->parameter->subpage_page;++k) r->proof->disk[k]=expected_parity(r,k);
    }
    if (is_patch(s)) for (unsigned id=1;id<s->stripe.allStripe;++id) {
        struct r4_row *r=s->raid4->rows[id]; if (!r) continue;
        for (unsigned slot=0;slot<3;++slot) {
            unsigned lpn=s->trip2Page[id].lpn[slot], state=s->dram->map->map_entry[lpn].state;
            for (unsigned k=0;k<s->parameter->subpage_page;++k)
                if (state & (1u<<k)) r->patch->data[slot][k]=payload(lpn,k,0);
        }
        for (unsigned k=0;k<s->parameter->subpage_page;++k) r->patch->disk[k]=patch_expected(r,k);
        r->patch->disk_state=r->parity_state;
        patch_check(s,r);
    }
    s->ssdToken = s->channelToken = s->chipToken = 0;
    s->allPage = s->parameter->page_block * s->parameter->block_plane * s->parameter->plane_die * s->parameter->die_chip * s->parameter->chip_num;
    if (s->parameter->aged) {
        if (!(s->parameter->aged_ratio >= 0 && s->parameter->aged_ratio < 1)) fail("aged ratio must be in [0,1)");
        uint64_t target = (uint64_t)(s->allPage * s->parameter->aged_ratio);
        while (s->allUsedPage < target) age_invalid_page(s);
    }
    /* Aging/preprocessing can consume an entire plane before any host program
     * has had a chance to enqueue the legacy post-write GC trigger. */
    if (!s->parameter->fast_gc && !s->parameter->dr_switch)
    for (unsigned ch=0;ch<s->parameter->channel_number;++ch)
    for (unsigned chip=0;chip<s->parameter->chip_channel[ch];++chip)
    for (unsigned die=0;die<s->parameter->die_chip;++die)
    for (unsigned plane=0;plane<s->parameter->plane_die;++plane) {
        struct plane_info *pl=&s->channel_head[ch].chip_head[chip].die_head[die].plane_head[plane];
        if (pl->free_page && pl->free_page >= s->parameter->page_block*s->parameter->block_plane*s->parameter->gc_hard_threshold) continue;
        struct gc_operation *g=calloc(1,sizeof(*g)); alloc_assert(g,"startup GC");
        g->chip=chip; g->die=die; g->plane=plane; g->block=UINT_MAX;
        g->state=GC_WAIT; g->priority=GC_UNINTERRUPT;
        struct channel_info *channel=&s->channel_head[ch];
        if (channel->last_gc_command) channel->last_gc_command->next_node=g; else channel->gc_command=g;
        channel->last_gc_command=g; ++s->gc_request;
    }
}
static int bfp_check_node(void *context, unsigned lpn, uint64_t version, unsigned mask, const uint64_t *values) {
    struct ssd_info *s=context;
    if (!lpn || lpn>s->stripe.checkStart) return 0;
    struct r4_row *r=s->raid4->rows[(lpn-1)/3+1]; unsigned slot=(lpn-1)%3;
    if (!r || r->bfp_version[slot]!=version || (s->dram->map->map_entry[lpn].state & mask)!=mask) return 0;
    if (values) for (unsigned k=0;k<s->parameter->subpage_page;++k)
        if ((mask & (1u<<k)) && (!r->proof || values[k]!=r->proof->current[slot][k])) return 0;
    return 1;
}
static void verify(struct ssd_info *s) {
    for (unsigned id = 1; id < s->stripe.allStripe; ++id) {
        struct r4_row *r = s->raid4->rows[id]; if (!r) continue;
        unsigned mask = 0;
        for (unsigned slot = 0; slot < 3; ++slot) {
            unsigned lpn = s->trip2Page[id].lpn[slot], state = s->dram->map->map_entry[lpn].state;
            if (!state) continue;
            struct local *loc = find_location(s, s->dram->map->map_entry[lpn].pn);
            struct page_info *page = physical(s, loc);
            if (loc->channel / 4 != slot || page->lpn != lpn || (unsigned)page->valid_state != state || page->raidID != id) fail("data mapping invariant violated");
            free(loc); mask |= state;
        }
        if (mask) {
            struct local *loc = s->trip2Page[id].location;
            if (!loc || loc->channel / 4 != 3 || physical(s, loc)->raidID != id || (unsigned)physical(s, loc)->valid_state != mask || s->trip2Page[id].parity_state != mask) fail("parity mapping invariant violated");
        }
        if (r->dirty || r->flushing || r->head || r->members || r->ppc_state || r->admit_queued || r->reclaim_credit) fail("unfinished stripe at verification");
        if (is_patch(s)) {
            patch_check(s,r);
            check(s,!r->patch->mask && !r->patch->reserved,"unfinished PATCH support/reservation");
            check(s,r->patch->disk_state==mask,"PATCH persisted state mismatch");
        }
        if (r->proof) for (unsigned k=0;k<s->parameter->subpage_page;++k)
            check(s,r->proof->disk[k]==expected_parity(r,k),"final persisted parity payload mismatch");
    }
    uint64_t free_types[3] = {0,0,0};
    for (unsigned ch = 0; ch < s->parameter->channel_number; ++ch)
        for (unsigned chip = 0; chip < s->parameter->chip_channel[ch]; ++chip)
            for (unsigned die = 0; die < s->parameter->die_chip; ++die)
                for (unsigned plane = 0; plane < s->parameter->plane_die; ++plane) {
                    struct plane_info *pl = &s->channel_head[ch].chip_head[chip].die_head[die].plane_head[plane];
                    uint64_t pages = 0, lsb = 0;
                    for (unsigned block = 0; block < s->parameter->block_plane; ++block) {
                        struct blk_info *bl = &pl->blk_head[block];
                        if (bl->free_page_num < 0 || bl->free_lsb_num + bl->free_csb_num + bl->free_msb_num != (unsigned)bl->free_page_num) fail("block free-page counters disagree");
                        pages += bl->free_page_num; lsb += bl->free_lsb_num;
                        free_types[0] += bl->free_lsb_num; free_types[1] += bl->free_csb_num; free_types[2] += bl->free_msb_num;
                    }
                    if (pages != pl->free_page || lsb != pl->free_lsb_num) fail("plane free-page counters disagree");
                }
    if (free_types[0] != s->free_lsb_count || free_types[1] != s->free_csb_count || free_types[2] != s->free_msb_count) fail("global free-page counters disagree");
    if (s->raid4->protected_pages) fail("protected baseline pages remain at EOF");
    if (s->gc_request) fail("unfinished GC at EOF");
    if (s->raid4->replacement_pending || s->raid4->flushing_count || s->raid4->admit_count || s->raid4->admit_demand || s->raid4->reclaim_pending_units) fail("unfinished cache reclaim at EOF");
    if (s->request_queue || s->request_queue_length || s->raid4->cache_count) fail("unfinished requests/cache at EOF");
    if (is_patch(s)) check(s,!s->raid4->patch_used && !s->raid4->patch_scratch,"unfinished PATCH occupancy at EOF");
    if (s->raid4->bfp) {
        check(s,!bfp_pending(s->raid4->bfp),"unfinished BFP resize");
        bfp_verify(s->raid4->bfp,s->raid4->cache_count,bfp_check_node,s);
    }
}
static void reset_member(struct ssd_info *s, unsigned member) {
    struct parameter_value *p = s->parameter;
    for (unsigned ch = member * 4; ch < member * 4 + 4; ++ch) {
        struct channel_info *channel = &s->channel_head[ch];
        while (channel->gc_command) { struct gc_operation *next = channel->gc_command->next_node; free(channel->gc_command); channel->gc_command = next; if (s->gc_request) --s->gc_request; }
        channel->last_gc_command = NULL;
        channel->current_state = channel->next_state = CHANNEL_IDLE;
        channel->next_state_predict_time = s->current_time;
        for (unsigned chip = 0; chip < p->chip_channel[ch]; ++chip) {
            struct chip_info *ci = &channel->chip_head[chip];
            ci->current_state = ci->next_state = CHIP_IDLE; ci->next_state_predict_time = s->current_time; ci->token = 0;
            for (unsigned die = 0; die < p->die_chip; ++die) for (unsigned plane = 0; plane < p->plane_die; ++plane) {
                struct plane_info *pl = &ci->die_head[die].plane_head[plane];
                while (pl->erase_node) { struct direct_erase *next = pl->erase_node->next_node; free(pl->erase_node); pl->erase_node = next; }
                for (unsigned block = 0; block < p->block_plane; ++block) {
                    struct blk_info *bl = &pl->blk_head[block];
                    s->free_lsb_count -= bl->free_lsb_num; s->free_csb_count -= bl->free_csb_num; s->free_msb_count -= bl->free_msb_num;
                    free(bl->page_head); memset(bl, 0, sizeof(*bl)); initialize_block(bl, p);
                    s->free_lsb_count += bl->free_lsb_num; s->free_csb_count += bl->free_csb_num; s->free_msb_count += bl->free_msb_num;
                }
                pl->free_page = p->block_plane * p->page_block;
                pl->free_lsb_num = p->block_plane * ((p->page_block + 2) / 3);
                pl->active_block = pl->active_lsb_block = pl->active_csb_block = pl->active_msb_block = 0; pl->add_reg_ppn = -1;
            }
        }
    }
}
void raid4_recover(struct ssd_info *s, unsigned member) {
    if (!s->raid4 || member>3) fail("invalid recovery member");
    struct raid4_controller *c=s->raid4;
    if (raid4_is_ppc(s) || is_patch(s)) settle_foreground(s); else { drain(s); verify(s); }
    for (unsigned id=1;id<s->stripe.allStripe;++id) {
        struct r4_row *r=c->rows[id]; if (!r) continue;
        if (r->dirty) ++c->recovery_uncommitted;
        if (member<3 && r->old_ppn[member]>=0) {
            --c->protected_pages; r->old_ppn[member]=-1;
            s->dram->map->map_entry[s->trip2Page[id].lpn[member]].oldppa=-1;
            r->force_current=1;
        }
    }
    if (c->bfp && member<3) bfp_invalidate_member(c->bfp,member);
    reset_member(s,member);
    s->recoveryTime=s->current_time; c->recovering=1; ++c->recovery_injections;
    for (unsigned id=1;id<s->stripe.allStripe;++id) {
        struct r4_row *r=c->rows[id]; if (!r || !r->parity_state) continue;
        unsigned lpn=member==3 ? s->stripe.checkLpn : s->trip2Page[id].lpn[member];
        unsigned mask=member==3 ? r->parity_state : s->dram->map->map_entry[lpn].state;
        if (!mask) continue;
        if (member==3) { free(s->trip2Page[id].location); s->trip2Page[id].location=NULL; s->trip2Page[id].parity_state=0; }
        else { s->dram->map->map_entry[lpn].state=0; s->dram->map->map_entry[lpn].pn=0; }
        struct r4_job *j=calloc(1,sizeof(*j)); alloc_assert(j,"recovery job");
        j->lpn=lpn; j->state=mask; j->operation=WRITE; j->rebuild=1;
        enqueue(s,r,j);
    }
    /* Reconstruct first, while PPC and baseline versions still exist. Only then
     * flush dirty stripes and resume normal writes on the replacement member. */
    drain(s); c->recovering=0; verify(s);
    c->recovery_elapsed += s->current_time-s->recoveryTime;
}
static double safe_ratio(uint64_t numerator, uint64_t denominator) {
    return denominator ? (double)numerator / denominator : 0;
}
static uint64_t free_pages(struct ssd_info *s, unsigned first, unsigned end) {
    uint64_t count = 0;
    for (unsigned ch=first;ch<end;++ch)
    for (unsigned chip=0;chip<s->parameter->chip_channel[ch];++chip)
    for (unsigned die=0;die<s->parameter->die_chip;++die)
    for (unsigned plane=0;plane<s->parameter->plane_die;++plane)
        count += s->channel_head[ch].chip_head[chip].die_head[die].plane_head[plane].free_page;
    return count;
}
static void periodic_statistics(struct ssd_info *s, int final) {
    struct raid4_controller *c=s->raid4;
    /* Terminal progress remains available with the compact result format. */
    if (!detailed_statistics) {
        if (!final) printf("RAID4 progress: %llu host requests completed.\n",c->completed);
        return;
    }
    uint64_t reads=s->read_request_count-c->period_reads, writes=s->write_request_count-c->period_writes;
    uint64_t read_ns=(uint64_t)s->read_avg-c->period_read_ns, write_ns=(uint64_t)s->write_avg-c->period_write_ns;
    struct r4_backend_stats *b=&c->backend[0];
    /* Period rows include normal-phase GC only; host times include checkpoint
     * pauses. A final zero-request row still reports EOF parity/GC work. */
    fprintf(s->statisticfile,"period,%s,%llu,%lld,%llu,%llu,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu\n",
        final ? "final" : "sample",c->completed,(long long)s->current_time,(unsigned long long)reads,(unsigned long long)writes,
        safe_ratio(read_ns,reads),safe_ratio(write_ns,writes),safe_ratio(read_ns+write_ns,reads+writes),
        (unsigned long long)b->erases,(unsigned long long)(b->erases-c->period_gc),
        (unsigned long long)b->moved,(unsigned long long)(b->moved-c->period_moved),
        (unsigned long long)free_pages(s,0,16),(unsigned long long)s->free_lsb_count);
    fflush(s->statisticfile);
    c->period_reads=s->read_request_count; c->period_writes=s->write_request_count;
    c->period_read_ns=s->read_avg; c->period_write_ns=s->write_avg;
    c->period_gc=b->erases; c->period_moved=b->moved;
    if (!final) printf("RAID4 progress: %llu host requests completed.\n",c->completed);
}
static void backend_statistics(struct ssd_info *s, unsigned phase) {
    FILE *f=s->statisticfile;
    struct r4_backend_stats *b=&s->raid4->backend[phase];
    const char *name=phase ? "recovery" : "normal";
#define BACKEND(key, value) fprintf(f,"%s_" key "=%llu\n",name,(unsigned long long)(value))
    BACKEND("nand_read_pages",b->reads); BACKEND("nand_program_pages",b->programs);
    BACKEND("nand_read_sectors",b->read_sectors); BACKEND("nand_write_transfer_sectors",b->write_transfer_sectors);
    BACKEND("gc_erases",b->erases); BACKEND("gc_moved_pages",b->moved);
    BACKEND("write_lsb_pages",b->page_type[0]); BACKEND("write_csb_pages",b->page_type[1]); BACKEND("write_msb_pages",b->page_type[2]);
    BACKEND("backend_read_wait_ns",b->wait_total[READ]); BACKEND("backend_write_wait_ns",b->wait_total[WRITE]);
    BACKEND("backend_read_samples",b->wait_count[READ]); BACKEND("backend_write_samples",b->wait_count[WRITE]);
    BACKEND("backend_read_wait_max_ns",b->wait_max[READ]); BACKEND("backend_write_wait_max_ns",b->wait_max[WRITE]);
    fprintf(f,"%s_backend_read_wait_avg_ns=%.3f\n%s_backend_write_wait_avg_ns=%.3f\n",name,
        safe_ratio(b->wait_total[READ],b->wait_count[READ]),name,safe_ratio(b->wait_total[WRITE],b->wait_count[WRITE]));
    for (unsigned ch=0;ch<16;++ch) {
        fprintf(f,"%s_channel_%u_reads=%llu\n%s_channel_%u_programs=%llu\n%s_channel_%u_erases=%llu\n",name,ch,
            (unsigned long long)b->channel_reads[ch],name,ch,(unsigned long long)b->channel_programs[ch],name,ch,(unsigned long long)b->channel_erases[ch]);
        fprintf(f,"%s_channel_%u_read_wait_ns=%llu\n%s_channel_%u_write_wait_ns=%llu\n",name,ch,
            (unsigned long long)b->channel_read_wait[ch],name,ch,(unsigned long long)b->channel_write_wait[ch]);
    }
    size_t planes=(size_t)16*2*s->parameter->die_chip*s->parameter->plane_die;
    for (unsigned ch=0;ch<16;++ch)
    for (unsigned chip=0;chip<2;++chip) {
        fprintf(f,"%s_channel_%u_chip_%u_erases=%llu\n",name,ch,chip,(unsigned long long)b->chip_erases[ch][chip]);
        for (unsigned die=0;die<s->parameter->die_chip;++die)
        for (unsigned plane=0;plane<s->parameter->plane_die;++plane) {
            size_t index=(((size_t)ch*2+chip)*s->parameter->die_chip+die)*s->parameter->plane_die+plane;
            fprintf(f,"%s_channel_%u_chip_%u_die_%u_plane_%u_erases=%llu\n",name,ch,chip,die,plane,
                (unsigned long long)s->raid4->plane_erases[phase*planes+index]);
        }
    }
    for (unsigned member=0;member<4;++member) {
        uint64_t reads=0,programs=0,erases=0;
        for (unsigned ch=member*4;ch<member*4+4;++ch) {
            reads+=b->channel_reads[ch]; programs+=b->channel_programs[ch]; erases+=b->channel_erases[ch];
        }
        fprintf(f,"%s_member_%u_reads=%llu\n%s_member_%u_programs=%llu\n%s_member_%u_erases=%llu\n",name,member,
            (unsigned long long)reads,name,member,(unsigned long long)programs,name,member,(unsigned long long)erases);
    }
#undef BACKEND
}
static void common_statistics(struct ssd_info *s, int64_t normal_end) {
    struct raid4_controller *c=s->raid4;
    FILE *f=s->statisticfile;
    struct r4_backend_stats *normal=&c->backend[0], *recovery=&c->backend[1];
    uint64_t duration=(uint64_t)(normal_end-c->normal_start);
    uint64_t host_bytes=c->write_sectors*(uint64_t)s->parameter->subpage_capacity;
    uint64_t program_bytes=normal->programs*(uint64_t)s->parameter->page_capacity;
#define COMMON(key, value) fprintf(f,key "=%llu\n",(unsigned long long)(value))
    if (detailed_statistics) {
        fprintf(f,"\n# Final common statistics; times in ns, request sizes in sectors.\n");
        COMMON("statistics_version",2); COMMON("period_interval_requests",10000);
        COMMON("physical_total_pages",s->allPage); COMMON("physical_data_pages",s->allPage/4*3);
        COMMON("physical_parity_pages",s->allPage/4); COMMON("user_data_pages",s->stripe.checkStart);
        COMMON("physical_total_bytes",s->allPage*(uint64_t)s->parameter->page_capacity);
        COMMON("user_data_bytes",s->stripe.checkStart*(uint64_t)s->parameter->page_capacity);
        COMMON("min_lsn",c->submitted ? c->min_lsn : 0); COMMON("max_lsn",c->max_lsn);
        COMMON("initial_nand_read_pages",c->init_reads); COMMON("initial_nand_program_pages",c->init_programs);
        COMMON("initial_used_pages",c->init_used_pages); COMMON("initial_free_pages",c->init_free_pages);
        COMMON("final_free_pages",free_pages(s,0,16));
        COMMON("normal_duration_ns",duration); COMMON("normal_recovery_pause_ns",c->normal_recovery_pause);
        COMMON("read_latency_total_ns",s->read_avg); COMMON("write_latency_total_ns",s->write_avg);
        COMMON("io_latency_total_ns",(uint64_t)s->read_avg+s->write_avg);
        COMMON("read_latency_max_ns",c->max_read_latency); COMMON("write_latency_max_ns",c->max_write_latency);
        fprintf(f,"read_request_average_size_sectors=%.6f\nwrite_request_average_size_sectors=%.6f\n",
            safe_ratio(c->read_sectors,s->read_request_count),safe_ratio(c->write_sectors,s->write_request_count));
        fprintf(f,"host_iops=%.6f\nhost_throughput_mib_s=%.6f\n",duration ? c->completed*1e9/duration : 0,
            duration ? (c->read_sectors+c->write_sectors)*(double)s->parameter->subpage_capacity*1e9/duration/1048576 : 0);
        fprintf(f,"parity_cache_hit_ratio=%.6f\n",safe_ratio(c->cache_hits,c->cache_hits+c->cache_misses));
        COMMON("normal_nand_program_bytes",program_bytes); COMMON("host_write_bytes",host_bytes);
        COMMON("nand_accounting_complete",!s->parameter->fast_gc && !s->parameter->dr_switch);
        COMMON("write_amplification_defined",host_bytes!=0 && !s->parameter->fast_gc && !s->parameter->dr_switch);
        fprintf(f,"write_amplification=%.6f\n",safe_ratio(program_bytes,host_bytes));
        backend_statistics(s,0); backend_statistics(s,1);
        uint64_t per_channel=s->allPage/16;
        for (unsigned ch=0;ch<16;++ch) {
            uint64_t free=free_pages(s,ch,ch+1);
            fprintf(f,"channel_%u_free_pages=%llu\nchannel_%u_free_ratio=%.6f\n",ch,(unsigned long long)free,ch,safe_ratio(free,per_channel));
        }
        for (unsigned member=0;member<4;++member) {
            uint64_t free=free_pages(s,member*4,member*4+4);
            fprintf(f,"member_%u_free_pages=%llu\nmember_%u_free_ratio=%.6f\n",member,(unsigned long long)free,member,safe_ratio(free,s->allPage/4));
        }
    }
    /* Familiar SSDSim labels, using the same verified counters as the keys.
     * Cumulative response time is deliberately distinct from average time. */
    fprintf(f,"\n---------------------------statistic data---------------------------\n");
    if (!detailed_statistics) fprintf(f,"trace file: %s\n",s->tracefilename);
    if (detailed_statistics)
    fprintf(f,"min lsn: %llu\nmax lsn: %llu\n",(unsigned long long)(c->submitted ? c->min_lsn : 0),(unsigned long long)c->max_lsn);
    fprintf(f,"read count: %llu\nprogram count: %llu\nwrite flash count: %llu\n",
        (unsigned long long)(c->init_reads+normal->reads+recovery->reads),
        (unsigned long long)(c->init_programs+normal->programs+recovery->programs),
        (unsigned long long)(normal->programs+recovery->programs));
    fprintf(f,"erase count: %llu\npage_move_count: %llu\n",(unsigned long long)(normal->erases+recovery->erases),
        (unsigned long long)(normal->moved+recovery->moved));
    if (detailed_statistics) fprintf(f,"write LSB count: %llu\nwrite CSB count: %llu\nwrite MSB count: %llu\n",
        (unsigned long long)(normal->page_type[0]+recovery->page_type[0]),
        (unsigned long long)(normal->page_type[1]+recovery->page_type[1]),
        (unsigned long long)(normal->page_type[2]+recovery->page_type[2]));
    fprintf(f,"read request count: %u\nwrite request count: %u\n",s->read_request_count,s->write_request_count);
    fprintf(f,"read request average size: %.6f\nwrite request average size: %.6f\n",
        safe_ratio(c->read_sectors,s->read_request_count),safe_ratio(c->write_sectors,s->write_request_count));
    fprintf(f,"read request average response time: %.3f\nwrite request average response time: %.3f\n",
        safe_ratio(s->read_avg,s->read_request_count),safe_ratio(s->write_avg,s->write_request_count));
    if (detailed_statistics) fprintf(f,"read response time: %llu\nwrite response time: %llu\nI/O response time: %llu\n",
        (unsigned long long)s->read_avg,(unsigned long long)s->write_avg,(unsigned long long)((uint64_t)s->read_avg+s->write_avg));
    fprintf(f,"I/O request average response time: %.3f\n",safe_ratio((uint64_t)s->read_avg+s->write_avg,c->completed));
    fprintf(f,"\n# Scheme statistics and verification\n");
#undef COMMON
}
/* Normal experiment output: keep comparisons readable. Full backend accounting
 * remains opt-in for audits/tests; this function only reads completed counters. */
static void compact_scheme_statistics(struct ssd_info *s, int64_t normal_end, int recovery_member) {
    struct raid4_controller *c=s->raid4;
    struct r4_backend_stats *b=&c->backend[0];
    FILE *f=s->statisticfile;
    uint64_t host_bytes=c->write_sectors*(uint64_t)s->parameter->subpage_capacity;
    uint64_t program_bytes=b->programs*(uint64_t)s->parameter->page_capacity;
    int accounting=!s->parameter->fast_gc && !s->parameter->dr_switch;
    uint64_t parity_programs=0;
    for (unsigned ch=0;ch<16;++ch) parity_programs+=c->parity_writes[ch];
#define KEY(name, value) fprintf(f,name "=%llu\n",(unsigned long long)(value))
    KEY("statistics_version",3);
    fprintf(f,"scheme=%s\ndata_pattern=%s\ncache_policy=lru\nnvram_assumed=1\n",
        parity_scheme_name(c->options.scheme),c->options.alternating_data ? "alternating" : "versioned");
    KEY("cache_capacity_bytes",s->parameter->dram_capacity);
    KEY("page_bytes",s->parameter->page_capacity); KEY("sectors_per_page",s->parameter->subpage_page);
    fprintf(f,"cache_allocation=%s\n",is_patch(s) ? "sector" : "page");
    if (!is_patch(s)) KEY("cache_entry_limit",c->bfp ? bfp_write_limit(c->bfp) : c->cache_limit);
    KEY("host_completed",c->completed);
    fprintf(f,"read_latency_ns=%.3f\nwrite_latency_ns=%.3f\nio_latency_ns=%.3f\n",
        safe_ratio(s->read_avg,s->read_request_count),safe_ratio(s->write_avg,s->write_request_count),
        safe_ratio((uint64_t)s->read_avg+s->write_avg,c->completed));
    KEY("simulated_duration_ns",normal_end-c->normal_start);
    KEY("nand_read_pages",b->reads); KEY("nand_program_pages",b->programs);
    KEY("write_amplification_defined",accounting && host_bytes!=0);
    fprintf(f,"write_amplification=%.6f\n",accounting ? safe_ratio(program_bytes,host_bytes) : 0);
    KEY("cache_hits",c->cache_hits); KEY("cache_misses",c->cache_misses);
    fprintf(f,"parity_cache_hit_ratio=%.6f\n",safe_ratio(c->cache_hits,c->cache_hits+c->cache_misses));
    KEY("cache_evictions",c->evictions); KEY("parity_reads",c->parity_reads);
    KEY("parity_program_pages",parity_programs);
    KEY("replacement_commits",c->commit_count[COMMIT_REPLACEMENT]); KEY("gc_commits",c->commit_count[COMMIT_GC]);
    KEY("eof_commits",c->commit_count[COMMIT_EOF]); KEY("sync_commits",c->commit_count[COMMIT_SYNC]);
    KEY("gc_count",s->erase_count); KEY("gc_moved_pages",s->moved_page_count);
    if (raid4_is_ppc(s)) {
        KEY("ppc_first_updates",c->update_first); KEY("ppc_new_member_updates",c->update_new_member);
        KEY("ppc_repeat_updates",c->update_repeat); KEY("ppc_update_reads",c->update_reads);
        KEY("commit_reads",c->commit_reads); KEY("commit_old_path",c->commit_old);
        KEY("commit_current_path",c->commit_current); KEY("peak_protected_pages",c->peak_protected);
    }
    if (is_patch(s)) {
        fprintf(f,"patch_data_model=synthetic_64bit_sector\npatch_value_check=PASS\n");
        KEY("patch_peak_entries",c->patch_peak_entries);
        KEY("patch_persistent_peak_bytes",c->patch_persistent_peak*SECTOR);
        KEY("patch_scratch_peak_bytes",c->patch_scratch_peak);
        KEY("patch_cancelled_sectors",c->patch_cancelled);
        KEY("patch_unchanged_written_sectors",c->patch_unchanged);
        KEY("patch_update_read_pages",c->patch_reads); KEY("patch_commit_read_pages",c->patch_commit_reads);
        KEY("patch_sync_commits",c->patch_sync);
    }
    if (c->bfp) {
        KEY("resize_commits",c->commit_count[COMMIT_RESIZE]);
        bfp_statistics(c->bfp,f,s->parameter->page_capacity,0);
    }
    if (recovery_member>=0) {
        fprintf(f,"rebuild_member=%d\nrebuild_after_requests=%lld\n",recovery_member,(long long)c->options.recover_after);
        KEY("rebuild_pages",c->recovered); KEY("rebuild_elapsed_ns",c->recovery_elapsed);
    }
    KEY("cache_dirty_at_end",0); KEY("gc_pending",s->gc_request);
    fprintf(f,"payload_check=%s\nmapping_check=PASS\nrun_status=COMPLETE\n",c->options.verify_data ? "PASS" : "disabled");
    fflush(f);
#undef KEY
}
static void statistics(struct ssd_info *s, int64_t normal_end, int recovery_member) {
    struct raid4_controller *c = s->raid4;
    FILE *f = s->statisticfile;
    common_statistics(s,normal_end);
    fprintf(f,"comparison_model=raid4_binary_xor_v1\nxor_model=binary_sector\nxor_full_page_ns=%u\n",RAID4_XOR_FULL_PAGE_NS);
    fprintf(f,"xor_sector_ops_total=%llu\nxor_service_ns_total=%llu\n",
        (unsigned long long)(c->xor_sector_ops[0]+c->xor_sector_ops[1]+c->xor_sector_ops[2]),
        (unsigned long long)(c->xor_service_ns[0]+c->xor_service_ns[1]+c->xor_service_ns[2]));
    if (detailed_statistics) {
        const char *phase[]={"update","commit","recovery"};
        for (unsigned i=0;i<3;++i) fprintf(f,"xor_%s_sector_ops=%llu\nxor_%s_service_ns=%llu\n",phase[i],
            (unsigned long long)c->xor_sector_ops[i],phase[i],(unsigned long long)c->xor_service_ns[i]);
    }
    if (!detailed_statistics) { compact_scheme_statistics(s,normal_end,recovery_member); return; }
#define STAT(name, value) fprintf(f, name "=%llu\n", (unsigned long long)(value))
    fprintf(f,"scheme=%s\n",parity_scheme_name(c->options.scheme));
    fprintf(f,"data_pattern=%s\n",c->options.alternating_data ? "alternating" : "versioned");
    fprintf(f,"payload_check=%s\n",c->options.verify_data ? "PASS" : "disabled");
    fprintf(f,"cache_policy=lru\nnvram_assumed=1\nrecovery_after_requests=%lld\n",(long long)c->options.recover_after);
    STAT("cache_capacity_bytes",s->parameter->dram_capacity);
    STAT("cache_entry_limit",is_patch(s) ? 0 : c->bfp ? bfp_write_limit(c->bfp) : c->cache_limit);
    STAT("page_bytes",s->parameter->page_capacity); STAT("sectors_per_page",s->parameter->subpage_page);
    fprintf(f,"cache_allocation=%s\n",is_patch(s) ? "sector" : "page");
    if (is_patch(s)) {
        fprintf(f,"patch_data_model=synthetic_64bit_sector\npatch_value_check=PASS\ncache_entry_limit_defined=0\n");
        STAT("patch_unit_bytes",SECTOR); STAT("patch_capacity_sectors",c->patch_limit);
        STAT("patch_effective_capacity_bytes",c->patch_limit*SECTOR); STAT("patch_peak_entries",c->patch_peak_entries);
        STAT("patch_used_bytes_at_end",c->patch_used*SECTOR); STAT("patch_peak_bytes",c->patch_peak*SECTOR);
        STAT("patch_persistent_peak_bytes",c->patch_persistent_peak*SECTOR); STAT("patch_scratch_peak_bytes",c->patch_scratch_peak);
        STAT("patch_updates",c->patch_updates); STAT("patch_created_sectors",c->patch_created);
        STAT("patch_merged_sectors",c->patch_merged); STAT("patch_cancelled_sectors",c->patch_cancelled);
        STAT("patch_unchanged_written_sectors",c->patch_unchanged); STAT("patch_update_read_pages",c->patch_reads);
        STAT("patch_commit_read_pages",c->patch_commit_reads); STAT("patch_sync_commits",c->patch_sync);
        STAT("patch_recovery_delta_stripes",c->patch_recovery_delta);
    }
    STAT("invariant_checks",c->checks);
    STAT("ppc_first_updates",c->update_first); STAT("ppc_new_member_updates",c->update_new_member);
    STAT("ppc_repeat_updates",c->update_repeat); STAT("ppc_update_reads",c->update_reads); STAT("partial_page_rmw_reads",c->rmw_reads);
    STAT("replacement_commits",c->commit_count[COMMIT_REPLACEMENT]); STAT("gc_commits",c->commit_count[COMMIT_GC]);
    STAT("eof_commits",c->commit_count[COMMIT_EOF]); STAT("sync_commits",c->commit_count[COMMIT_SYNC]);
    STAT("commit_reads",c->commit_reads); STAT("commit_old_path",c->commit_old); STAT("commit_current_path",c->commit_current);
    STAT("protected_pages_at_end",c->protected_pages); STAT("peak_protected_pages",c->peak_protected); STAT("gc_waits",c->gc_waits);
    STAT("gc_write_yields",c->gc_write_yields);
    STAT("recovery_uncommitted_stripes",c->recovery_uncommitted); STAT("recovery_partial_pages",c->recovery_partial);
    STAT("recovery_old_parity_pages",c->recovery_old); STAT("recovery_injections",c->recovery_injections);
    STAT("host_submitted", c->submitted); STAT("host_completed", c->completed);
    STAT("read_requests", s->read_request_count); STAT("write_requests", s->write_request_count);
    STAT("host_read_sectors", c->read_sectors); STAT("host_write_sectors", c->write_sectors);
    STAT("preprocessed_data_pages", c->pre_data); STAT("preprocessed_parity_pages", c->pre_parity);
    STAT("normal_end_ns", normal_end); STAT("cache_dirty_at_end", 0);
    STAT("replacement_pending_at_end",c->replacement_pending); STAT("reclaim_pending_units_at_end",c->reclaim_pending_units);
    STAT("cache_admission_waiters_at_end",c->admit_count);
    STAT("replacement_pending_peak",c->replacement_pending_peak); STAT("victim_scan_steps",c->victim_scan_steps);
    fprintf(f,"cache_reclaim_unit=%s\n",is_patch(s) ? "sector" : "page_entry");
    STAT("cache_hits", c->cache_hits); STAT("cache_misses", c->cache_misses); STAT("cache_evictions", c->evictions);
    STAT("xor_jobs", c->xor_jobs); STAT("parity_reads", c->parity_reads);
    STAT("gc_count", s->erase_count); STAT("gc_pending", s->gc_request); STAT("gc_moved_pages", s->moved_page_count);
    STAT("recovered_pages", c->recovered); STAT("recovery_reads", c->recovery_reads);
    STAT("recovery_elapsed_ns", c->recovery_elapsed);
    fprintf(f, "recovery_member=%d\n", recovery_member);
    for (unsigned ch = 0; ch < 16; ++ch) {
        fprintf(f, "data_writes_channel_%u=%llu\nparity_writes_channel_%u=%llu\n", ch, c->data_writes[ch], ch, c->parity_writes[ch]);
    }
    fprintf(f, "read_latency_ns=%.3f\nwrite_latency_ns=%.3f\n", s->read_request_count ? (double)s->read_avg / s->read_request_count : 0,
            s->write_request_count ? (double)s->write_avg / s->write_request_count : 0);
    fprintf(f,"io_latency_ns=%.3f\n",c->completed ? ((double)s->read_avg+(double)s->write_avg)/c->completed : 0);
    fprintf(f, "mapping_check=PASS\nrun_status=COMPLETE\n"); fflush(f);
    if (c->bfp) {
        STAT("resize_commits",c->commit_count[COMMIT_RESIZE]);
        bfp_statistics(c->bfp,f,s->parameter->page_capacity,1); fflush(f);
    }
#undef STAT
}
void raid4_run(struct ssd_info *s, int rounds, const struct parity_options *options) {
    struct raid4_controller *c = calloc(1, sizeof(*c)); alloc_assert(c, "raid4 controller"); s->raid4 = c; c->options=*options;
    c->min_lsn=UINT64_MAX;
    size_t planes=(size_t)16*2*s->parameter->die_chip*s->parameter->plane_die;
    c->plane_erases=calloc(2*planes,sizeof(*c->plane_erases)); alloc_assert(c->plane_erases,"phase plane erase counters");
    c->rows = calloc(s->stripe.allStripe, sizeof(*c->rows)); alloc_assert(c->rows, "raid4 rows");
    c->cache_limit = s->parameter->dram_capacity / s->parameter->page_capacity; if (!c->cache_limit) c->cache_limit = 1;
    c->patch_limit=s->parameter->dram_capacity/SECTOR;
    if (getenv("RAID4_TRACE_EVENTS")) { c->events = raid4_result_open(".events", "w"); if (!c->events) fail("cannot create event log"); }
    s->cpu_current_state = CPU_IDLE; s->cpu_next_state_predict_time = s->current_time;
    printf("RAID4: 3 data SSDs + 1 parity SSD; channels 12-15 contain parity.\n");
    if (s->parameter->fast_gc || s->parameter->dr_switch)
        fail("RAID4 comparison requires fast_gc=0 and dr_switch=0 (ordinary GC)");
    if (s->parameter->subpage_capacity!=SECTOR || s->parameter->page_capacity!=SECTOR*s->parameter->subpage_page)
        fail("RAID4 comparison requires 512-byte sectors and page_bytes=512*sectors_per_page");
    c->experiment=experiment_stats_create(parity_scheme_name(options->scheme),strrchr(result_prefix,'/')+1);
    if (options->scheme==PARITY_BFP) {
        const struct bfp_config *config=NULL;
#ifdef BFP_TESTING
        extern const struct bfp_config *bfp_test_configuration(void);
        config=bfp_test_configuration();
#endif
        c->bfp=bfp_create(s->parameter->dram_capacity/s->parameter->page_capacity,
                          s->parameter->subpage_page,options->verify_data,config);
        c->cache_limit=bfp_write_limit(c->bfp); if (!c->cache_limit) c->cache_limit=1;
        if (detailed_statistics) {
            fprintf(s->statisticfile,"# bfp_window,window,completed,time_ns,phase,read_events,parity_events,read_sum,parity_sum,raw,ema,bounded,H,S,parity_pages,read_pages,goal,moved\n");
            fprintf(s->statisticfile,"# bfp_transfer,window,time_ns,phase,old_parity,new_parity,moved\n");
        }
    }
    if (is_patch(s)) printf("PATCH synthetic content model: %s; 512-byte patches, LRU eviction.\n",options->alternating_data ? "alternating" : "versioned");
    printf("Parity scheme: %s; synthetic payload verification: %s\n",parity_scheme_name(options->scheme),options->verify_data ? "enabled" : "disabled");
    preprocess(s);
    c->init_reads=s->read_count; c->init_programs=s->program_count;
    c->init_free_pages=free_pages(s,0,16); c->init_used_pages=s->allUsedPage; c->normal_start=s->current_time;
    if (detailed_statistics) {
        fprintf(s->statisticfile,"\n# Period statistics: 10000 completed host requests per sample; final includes EOF commits.\n");
        fprintf(s->statisticfile,"# period,kind,completed,time_ns,window_read_requests,window_write_requests,read_avg_ns,write_avg_ns,io_avg_ns,normal_gc_total,normal_gc_window,normal_moved_total,normal_moved_window,free_pages,free_lsb_pages\n");
        fflush(s->statisticfile);
    }
    if (options->recover_after==0) raid4_recover(s,(unsigned)options->recovery_member);
    for (int round = 0; round < rounds; ++round) {
        FILE *f = fopen(s->tracefilename, "r"); if (!f) fail("cannot reopen trace");
        struct trace_record record; int has = read_record(f, &record);
        int64_t first = has ? record.time : 0, base = s->current_time, previous = first;
        while (has || c->active || s->request_queue || resize_pending(s)) {
            if (options->recover_after>0 && !c->recovery_injections && c->submitted>=(uint64_t)options->recover_after)
                raid4_recover(s,(unsigned)options->recovery_member);
            if (has && record.time < previous) fail("trace timestamps must be nondecreasing");
            int64_t arrival = has ? base + record.time - first : MAX_INT64;
            unsigned limit = s->parameter->queue_length ? s->parameter->queue_length : 32;
            if (has && arrival <= s->current_time && s->request_queue_length < limit) {
                submit(s, &record, arrival); previous = record.time; has = read_record(f, &record);
                continue;
            }
            pump(s);
            /* Completion may have freed a queue slot. Admit an already-arrived
             * request at this time before selecting any future hardware event. */
            if (has && arrival <= s->current_time && s->request_queue_length < limit) continue;
            if (!has && !c->active && !s->request_queue && !resize_pending(s)) break;
            int64_t next = next_event(s);
            if (has && s->request_queue_length < limit && arrival > s->current_time && arrival < next) next = arrival;
            if (next == MAX_INT64) { advance(s); }
            else s->current_time = next;
        }
        fclose(f); drain(s);
        if (c->bfp) { bfp_clear_reads(c->bfp); bfp_observe(c->bfp,c->cache_count); }
        verify(s);
    }
    int64_t normal_end = s->current_time;
    c->normal_recovery_pause=c->recovery_elapsed;
    periodic_statistics(s,1);
    if (options->recover_after>=0 && !c->recovery_injections) {
        if (c->submitted==(uint64_t)options->recover_after) raid4_recover(s,(unsigned)options->recovery_member);
        else fail("--recover-after exceeds the trace request count");
    }
    if (options->recovery_member>=0 && options->recover_after<0) raid4_recover(s,(unsigned)options->recovery_member);
    experiment_stats_finish(c->experiment);
    statistics(s,normal_end,options->recovery_member);
    printf("RAID4 completed: %llu/%llu host requests; mapping checks passed.\n", c->completed, c->submitted);
}
void raid4_destroy(struct ssd_info *s) {
    struct raid4_controller *c = s->raid4; if (!c) return;
    for (unsigned id = 1; id < s->stripe.allStripe; ++id) {
        if(c->rows[id]) { free(c->rows[id]->proof); free(c->rows[id]->patch); }
        free(c->rows[id]);
    }
    free(c->rows); free(c->plane_erases); if (c->events) fclose(c->events); bfp_destroy(c->bfp);
    experiment_stats_destroy(c->experiment); free(c); s->raid4 = NULL;
    if (s->outputfile) fclose(s->outputfile);
    if (s->statisticfile) fclose(s->statisticfile);
    if (s->raidOutfile) fclose(s->raidOutfile);
    if (s->gcCreateRequest) fclose(s->gcCreateRequest);
    if (s->gcIntervalFile) fclose(s->gcIntervalFile);
    if (s->motivateFile) fclose(s->motivateFile);
}
