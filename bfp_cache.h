#ifndef BFP_CACHE_H
#define BFP_CACHE_H
#include <stdint.h>
#include <stdio.h>

#define BFP_MODEL "bfp_lru_frequency_window_v1"
#define BFP_IMPLEMENTATION_REVISION "20261021_r1"
struct bfp_cache;
struct bfp_config {
    uint64_t window;
    unsigned warmup_windows, step_div, hit_ns;
    /* Internal test injection; production passes NULL for the default profile. */
    unsigned initial_w;
    int adaptive;
};
enum bfp_read_action { BFP_HIT, BFP_MERGE, BFP_REPEAT, BFP_COLD_SPACE, BFP_GHOST_ONLY };
struct bfp_read_token {
    unsigned lpn, mask;
    uint64_t version;
    enum bfp_read_action action;
    int consumed;
    uint64_t values[32];
};
struct bfp_stats {
    uint64_t reads, hits, misses, partial_misses, ghost_queries, ghost_hits;
    uint64_t fills, merges, ghost_only, cold_drops, stale_drops, invalidations, stale_nodes;
    uint64_t evictions, resize_evictions, read_events, parity_events;
    uint64_t windows, optimizations, proposals, transfers, moved, deferred;
    uint64_t peak_read, peak_parity, peak_total, scratch_peak;
};
void bfp_default_config(struct bfp_config *cfg);
struct bfp_cache *bfp_create(unsigned slots, unsigned sectors, int verify, const struct bfp_config *cfg);
void bfp_destroy(struct bfp_cache *b);
unsigned bfp_slots(const struct bfp_cache *b);
unsigned bfp_write_limit(const struct bfp_cache *b);
unsigned bfp_read_limit(const struct bfp_cache *b);
unsigned bfp_admit_limit(const struct bfp_cache *b);
unsigned bfp_read_count(const struct bfp_cache *b);
unsigned bfp_hit_ns(const struct bfp_cache *b);
int bfp_pending(const struct bfp_cache *b);
int bfp_read_begin(struct bfp_cache *b, unsigned lpn, unsigned mask, uint64_t version,
                   const uint64_t *expected, struct bfp_read_token *token);
void bfp_read_finish(struct bfp_cache *b, struct bfp_read_token *token,
                     unsigned read_mask, uint64_t version);
void bfp_invalidate_write(struct bfp_cache *b, unsigned lpn);
void bfp_invalidate_member(struct bfp_cache *b, unsigned member);
void bfp_clear_reads(struct bfp_cache *b);
void bfp_note_parity(struct bfp_cache *b, unsigned stripe);
/* No signal returns 0 and leaves target=current. Inputs are never modified. */
int bfp_choose_target(const uint64_t *w, unsigned wn, const uint64_t *r, unsigned rn,
                      unsigned slots, unsigned current, unsigned *target);
/* Returns nonzero when a proposal is created; never performs NAND work. */
int bfp_host_complete(struct bfp_cache *b, uint64_t completed, int64_t now, FILE *detail);
/* Atomically transfers only released slots; controller owns donor NAND writes. */
int bfp_advance_resize(struct bfp_cache *b, unsigned parity_count, int64_t now, FILE *detail);
void bfp_observe(struct bfp_cache *b, unsigned parity_count);
typedef int (*bfp_node_check)(void *, unsigned, uint64_t, unsigned, const uint64_t *);
void bfp_verify(struct bfp_cache *b, unsigned parity_count, bfp_node_check check, void *context);
const struct bfp_stats *bfp_get_stats(const struct bfp_cache *b);
void bfp_statistics(const struct bfp_cache *b, FILE *f, unsigned page_bytes, int detailed);
#endif
