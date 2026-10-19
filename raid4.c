#include "raid4.h"
#include <assert.h>
#include <limits.h>

/* Requests in one stripe are serialized. Different stripes retain NAND parallelism.
 * Each dirty cache line represents the newest parity, including all acknowledged writes.
 * Only idle lines can be evicted; a line stays pinned until its NAND write completes.
 * The legacy SSDSim stores metadata, not bytes; XOR_process models computation time.
 */
enum { JOB_NEW, JOB_READS, JOB_XOR, JOB_WRITES };
struct r4_job {
    struct request io;
    struct request *host;
    struct r4_job *next;
    unsigned lpn, state, operation, stage;
    int rebuild;
    int64_t ready;
};
struct r4_row {
    unsigned id, parity_state;
    int cached, dirty, flushing, active;
    struct r4_job *head, *tail;
    struct request flush_io;
    struct r4_row *next_active, *lru_prev, *lru_next;
};
struct raid4_controller {
    struct r4_row **rows, *active, *lru_head, *lru_tail;
    unsigned cache_limit, cache_count;
    unsigned long long submitted, completed, read_sectors, write_sectors;
    unsigned long long data_writes[16], parity_writes[16], parity_reads;
    unsigned long long cache_hits, cache_misses, evictions, xor_jobs;
    unsigned long long recovered, recovery_reads, pre_data, pre_parity;
    int recovering;
    FILE *events;
};
static void fail(const char *message) {
    fprintf(stderr, "RAID4: %s\n", message);
    exit(EXIT_FAILURE);
}
static unsigned full_state(struct ssd_info *s) {
    return s->parameter->subpage_page == 32 ? UINT32_MAX : (1u << s->parameter->subpage_page) - 1u;
}
static struct page_info *physical(struct ssd_info *s, const struct local *l) {
    return &s->channel_head[l->channel].chip_head[l->chip].die_head[l->die].plane_head[l->plane].blk_head[l->block].page_head[l->page];
}
static unsigned channel_for(unsigned stripe, unsigned member) { return member * 4 + (stripe - 1) % 4; }
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
    struct sub_request *q = creat_sub_request(s, lpn, size(mask), mask, io, op, TARGET_LSB, r->id);
    if (!q) fail("NAND request allocation failed");
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
static void start_flush(struct ssd_info *s, struct r4_row *r) {
    if (r->flushing || !r->dirty) return;
    if (r->head && r->head->stage != JOB_NEW) fail("attempt to evict a pinned parity line");
    r->flushing = 1;
    issue(s, r, &r->flush_io, s->stripe.checkLpn, r->parity_state, WRITE);
    activate(s->raid4, r);
}
static int acquire_cache(struct ssd_info *s, struct r4_row *r) {
    struct raid4_controller *c = s->raid4;
    if (r->flushing) return 0;
    if (r->cached) { ++c->cache_hits; lru_touch(c, r); return 1; }
    if (c->cache_count >= c->cache_limit) {
        struct r4_row *victim = c->lru_tail;
        while (victim && (victim->flushing || (victim->head && victim->head->stage != JOB_NEW))) victim = victim->lru_prev;
        if (!victim) return 0;
        ++c->evictions;
        if (victim->dirty) { start_flush(s, victim); return 0; }
        lru_remove(c, victim); victim->cached = 0; --c->cache_count;
    }
    ++c->cache_misses; lru_touch(c, r);
    return 1;
}
static void enqueue(struct ssd_info *s, struct r4_row *r, struct r4_job *j) {
    if (r->tail) r->tail->next = j; else r->head = j;
    r->tail = j; activate(s->raid4, r);
}
static void finish_job(struct ssd_info *s, struct r4_row *r) {
    struct r4_job *j = r->head;
    release_io(&j->io);
    if (j->host) {
        ++j->host->now;
        j->host->response_time = s->current_time;
    }
    if (j->rebuild) ++s->raid4->recovered;
    r->head = j->next; if (!r->head) r->tail = NULL;
    free(j);
}
static void poll_controller(struct ssd_info *s) {
    struct raid4_controller *c = s->raid4;
    if (s->cpu_next_state_predict_time <= s->current_time) s->cpu_current_state = CPU_IDLE;
    struct r4_row **link = &c->active;
    while (*link) {
        struct r4_row *r = *link;
        if (r->flushing && io_done(s, &r->flush_io)) {
            release_io(&r->flush_io);
            r->flushing = r->dirty = 0;
            lru_remove(c, r); r->cached = 0; --c->cache_count;
        }
        struct r4_job *j = r->head;
        if (j && !r->flushing) {
            if (j->stage == JOB_NEW) {
                if (j->rebuild) {
                    for (unsigned slot = 0; slot < 3; ++slot) {
                        unsigned lpn = s->trip2Page[r->id].lpn[slot];
                        if (lpn != j->lpn) issue(s, r, &j->io, lpn, s->dram->map->map_entry[lpn].state, READ);
                    }
                    if (j->lpn != s->stripe.checkLpn) issue(s, r, &j->io, s->stripe.checkLpn, s->trip2Page[r->id].parity_state, READ);
                    j->stage = JOB_READS;
                } else if (j->operation == READ) {
                    if ((s->dram->map->map_entry[j->lpn].state & j->state) != j->state) fail("read includes an uninitialized sector");
                    issue(s, r, &j->io, j->lpn, j->state, READ);
                    j->stage = JOB_READS;
                } else {
                    int was_cached = r->cached;
                    if (acquire_cache(s, r)) {
                        unsigned old = s->dram->map->map_entry[j->lpn].state;
                        /* Read the old page once: both old changed sectors for the parity delta
                         * and unchanged sectors needed for out-of-place NAND programming. */
                        issue(s, r, &j->io, j->lpn, old, READ);
                        if (!was_cached && s->trip2Page[r->id].location)
                            issue(s, r, &j->io, s->stripe.checkLpn, s->trip2Page[r->id].parity_state, READ);
                        j->stage = JOB_READS;
                    }
                }
            }
            if (j->stage == JOB_READS && io_done(s, &j->io)) {
                release_io(&j->io);
                if (j->operation == READ && !j->rebuild) { finish_job(s, r); j = NULL; }
                else {
                    XOR_process(s, size(j->state)); ++c->xor_jobs;
                    j->ready = s->cpu_next_state_predict_time;
                    j->stage = JOB_XOR;
                }
            }
            if (j && j->stage == JOB_XOR && j->ready <= s->current_time) {
                unsigned state = j->state;
                if (j->lpn != s->stripe.checkLpn) state |= s->dram->map->map_entry[j->lpn].state;
                issue(s, r, &j->io, j->lpn, state, WRITE);
                j->stage = JOB_WRITES;
            }
            if (j && j->stage == JOB_WRITES && io_done(s, &j->io)) {
                if (!j->rebuild) { r->parity_state |= j->state; r->dirty = 1; }
                finish_job(s, r);
                /* dram=0 is write-through, using one controller scratch page. */
                if (s->parameter->dram_capacity < s->parameter->page_capacity && r->dirty) start_flush(s, r);
            }
        }
        if (!r->head && !r->flushing) { *link = r->next_active; r->active = 0; r->next_active = NULL; }
        else link = &r->next_active;
    }
}
static void complete_hosts(struct ssd_info *s) {
    struct request **link = &s->request_queue;
    while (*link) {
        struct request *r = *link;
        if (r->now != r->all) { link = &r->next_node; continue; }
        int64_t latency = r->response_time - r->time;
        if (latency < 0) fail("negative host latency");
        fprintf(s->outputfile, "%lld %u %u %u %lld %lld %lld\n", (long long)r->time, r->lsn, r->size, r->operation,
                (long long)r->time, (long long)r->response_time, (long long)latency);
        if (r->operation == READ) { ++s->read_request_count; s->read_avg += latency; }
        else { ++s->write_request_count; s->write_avg += latency; }
        ++s->completed_request_count; ++s->raid4->completed;
        --s->request_queue_length;
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
        if (r->head && r->head->stage == JOB_XOR) CONSIDER(r->head->ready);
        struct request *io = r->flushing ? &r->flush_io : r->head ? &r->head->io : NULL;
        if (io) for (struct sub_request *q = io->subs; q; q = q->next_subs) CONSIDER(q->next_state_predict_time);
    }
#undef CONSIDER
    return next;
}
static void pump(struct ssd_info *s) {
    /* NAND first removes completed reads from its queues; then owners may release them. */
    process(s); poll_controller(s); complete_hosts(s);
    process(s);
}
static void advance(struct ssd_info *s) {
    int64_t next = next_event(s);
    if (next == MAX_INT64) {
        /* A predecessor may just have completed; give newly exposed jobs one turn. */
        pump(s); next = next_event(s);
        if (!s->raid4->active && !s->request_queue) return;
        if (next == MAX_INT64) fail("pending requests have no runnable NAND/CPU event");
    }
    s->current_time = next;
}
static void drain(struct ssd_info *s) {
    while (s->raid4->active || s->request_queue) { pump(s); if (s->raid4->active || s->request_queue) advance(s); }
    /* EOF flushes are independent across stripes; preserve parity-member parallelism. */
    struct r4_row *line = s->raid4->lru_tail;
    while (line) {
        struct r4_row *previous = line->lru_prev;
        if (line->dirty) start_flush(s, line);
        else { lru_remove(s->raid4, line); line->cached = 0; --s->raid4->cache_count; }
        line = previous;
    }
    while (s->raid4->active) { pump(s); if (s->raid4->active) advance(s); }
    /* Finish any remaining channel/chip busy intervals before EOF or member replacement. */
    int64_t next;
    while ((next = next_event(s)) != MAX_INT64) { s->current_time = next; process(s); }
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
    if (s->request_tail) s->request_tail->next_node = host; else s->request_queue = host;
    s->request_tail = host; ++s->request_queue_length; ++s->raid4->submitted;
    if (host->operation == READ) s->raid4->read_sectors += host->size; else s->raid4->write_sectors += host->size;
    uint64_t capacity = (uint64_t)s->stripe.checkStart * s->parameter->subpage_page;
    uint64_t cursor = host->lsn % capacity; unsigned remaining = host->size;
    while (remaining) {
        unsigned offset = cursor % s->parameter->subpage_page;
        unsigned count = s->parameter->subpage_page - offset; if (count > remaining) count = remaining;
        unsigned lpn = (unsigned)(cursor / s->parameter->subpage_page) + 1;
        struct r4_row *row = row_for(s, (lpn - 1) / 3 + 1);
        struct r4_job *j = calloc(1, sizeof(*j)); alloc_assert(j, "RAID4 job");
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
    s->ssdToken = s->channelToken = s->chipToken = 0;
    s->allPage = s->parameter->page_block * s->parameter->block_plane * s->parameter->plane_die * s->parameter->die_chip * s->parameter->chip_num;
    if (s->parameter->aged) {
        uint64_t target = (uint64_t)(s->allPage * s->parameter->aged_ratio);
        if (s->parameter->aged_ratio < 0 || s->parameter->aged_ratio >= 1) fail("aged ratio must be in [0,1)");
        while (s->allUsedPage < target) raid_pre_invalid(s);
    }
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
        if (r->dirty || r->flushing || r->head) fail("unfinished stripe at verification");
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
    if (s->gc_request) fail("unfinished GC at EOF");
    if (s->request_queue || s->request_queue_length || s->raid4->cache_count) fail("unfinished requests/cache at EOF");
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
    if (!s->raid4 || member > 3) fail("invalid recovery member");
    drain(s); verify(s);
    reset_member(s, member);
    s->recoveryTime = s->current_time; s->raid4->recovering = 1;
    for (unsigned id = 1; id < s->stripe.allStripe; ++id) {
        struct r4_row *r = s->raid4->rows[id]; if (!r || !r->parity_state) continue;
        unsigned lpn = member == 3 ? s->stripe.checkLpn : s->trip2Page[id].lpn[member];
        unsigned mask = member == 3 ? r->parity_state : s->dram->map->map_entry[lpn].state;
        if (!mask) continue;
        if (member == 3) { free(s->trip2Page[id].location); s->trip2Page[id].location = NULL; s->trip2Page[id].parity_state = 0; }
        else { s->dram->map->map_entry[lpn].state = 0; s->dram->map->map_entry[lpn].pn = 0; }
        struct r4_job *j = calloc(1, sizeof(*j)); alloc_assert(j, "recovery job");
        j->lpn = lpn; j->state = mask; j->operation = WRITE; j->rebuild = 1;
        enqueue(s, r, j);
    }
    drain(s); s->raid4->recovering = 0; verify(s);
}
static void statistics(struct ssd_info *s, int64_t normal_end, int recovery_member) {
    struct raid4_controller *c = s->raid4;
    FILE *f = fopen("raid4.stats", "w"); if (!f) fail("cannot create raid4.stats");
#define STAT(name, value) fprintf(f, name "=%llu\n", (unsigned long long)(value))
    STAT("host_submitted", c->submitted); STAT("host_completed", c->completed);
    STAT("read_requests", s->read_request_count); STAT("write_requests", s->write_request_count);
    STAT("host_read_sectors", c->read_sectors); STAT("host_write_sectors", c->write_sectors);
    STAT("preprocessed_data_pages", c->pre_data); STAT("preprocessed_parity_pages", c->pre_parity);
    STAT("normal_end_ns", normal_end); STAT("cache_dirty_at_end", 0);
    STAT("cache_hits", c->cache_hits); STAT("cache_misses", c->cache_misses); STAT("cache_evictions", c->evictions);
    STAT("xor_jobs", c->xor_jobs); STAT("parity_reads", c->parity_reads);
    STAT("gc_count", s->erase_count); STAT("gc_pending", s->gc_request); STAT("gc_moved_pages", s->moved_page_count);
    STAT("recovered_pages", c->recovered); STAT("recovery_reads", c->recovery_reads);
    STAT("recovery_elapsed_ns", recovery_member < 0 ? 0 : s->current_time - s->recoveryTime);
    fprintf(f, "recovery_member=%d\n", recovery_member);
    for (unsigned ch = 0; ch < 16; ++ch) {
        fprintf(f, "data_writes_channel_%u=%llu\nparity_writes_channel_%u=%llu\n", ch, c->data_writes[ch], ch, c->parity_writes[ch]);
    }
    fprintf(f, "read_latency_ns=%.3f\nwrite_latency_ns=%.3f\n", s->read_request_count ? (double)s->read_avg / s->read_request_count : 0,
            s->write_request_count ? (double)s->write_avg / s->write_request_count : 0);
    fprintf(f, "mapping_check=PASS\n"); fclose(f);
    FILE *copy = fopen("raid4.stats", "r");
    if (copy) { char line[256]; while (fgets(line, sizeof(line), copy)) fputs(line, s->statisticfile); fclose(copy); }
#undef STAT
}
void raid4_run(struct ssd_info *s, int rounds, int recovery_member) {
    struct raid4_controller *c = calloc(1, sizeof(*c)); alloc_assert(c, "raid4 controller"); s->raid4 = c;
    c->rows = calloc(s->stripe.allStripe, sizeof(*c->rows)); alloc_assert(c->rows, "raid4 rows");
    c->cache_limit = s->parameter->dram_capacity / s->parameter->page_capacity; if (!c->cache_limit) c->cache_limit = 1;
    if (getenv("RAID4_TRACE_EVENTS")) { c->events = fopen("raid4.events", "w"); if (!c->events) fail("cannot create event log"); }
    s->cpu_current_state = CPU_IDLE; s->cpu_next_state_predict_time = s->current_time;
    printf("RAID4: 3 data SSDs + 1 parity SSD; channels 12-15 contain parity.\n");
    preprocess(s);
    for (int round = 0; round < rounds; ++round) {
        FILE *f = fopen(s->tracefilename, "r"); if (!f) fail("cannot reopen trace");
        struct trace_record record; int has = read_record(f, &record);
        int64_t first = has ? record.time : 0, base = s->current_time, previous = first;
        while (has || c->active || s->request_queue) {
            if (has && record.time < previous) fail("trace timestamps must be nondecreasing");
            int64_t arrival = has ? base + record.time - first : MAX_INT64;
            unsigned limit = s->parameter->queue_length ? s->parameter->queue_length : 32;
            if (has && arrival <= s->current_time && s->request_queue_length < limit) {
                submit(s, &record, arrival); previous = record.time; has = read_record(f, &record);
                continue;
            }
            pump(s);
            if (!has && !c->active && !s->request_queue) break;
            int64_t next = next_event(s);
            if (has && s->request_queue_length < limit && arrival > s->current_time && arrival < next) next = arrival;
            if (next == MAX_INT64) { advance(s); }
            else s->current_time = next;
        }
        fclose(f); drain(s); verify(s);
    }
    int64_t normal_end = s->current_time;
    if (recovery_member >= 0) raid4_recover(s, recovery_member);
    statistics(s, normal_end, recovery_member);
    printf("RAID4 completed: %llu/%llu host requests; mapping checks passed.\n", c->completed, c->submitted);
}
void raid4_destroy(struct ssd_info *s) {
    struct raid4_controller *c = s->raid4; if (!c) return;
    for (unsigned id = 1; id < s->stripe.allStripe; ++id) free(c->rows[id]);
    free(c->rows); if (c->events) fclose(c->events); free(c); s->raid4 = NULL;
    if (s->outputfile) fclose(s->outputfile);
    if (s->statisticfile) fclose(s->statisticfile);
    if (s->raidOutfile) fclose(s->raidOutfile);
    if (s->gcCreateRequest) fclose(s->gcCreateRequest);
    if (s->gcIntervalFile) fclose(s->gcIntervalFile);
    if (s->motivateFile) fclose(s->motivateFile);
}
