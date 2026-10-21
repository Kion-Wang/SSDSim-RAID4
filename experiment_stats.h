#ifndef EXPERIMENT_STATS_H
#define EXPERIMENT_STATS_H
#include <stdint.h>

struct experiment_stats;
struct experiment_request;
/* All times are simulated nanoseconds; READ=1, WRITE=0. No scheduling here. */
struct experiment_stats *experiment_stats_create(const char *scheme, const char *stem);
struct experiment_request *experiment_request_create(unsigned operation);
void experiment_read_query(struct experiment_stats *stats, int hit);
void experiment_parity_access(struct experiment_stats *stats, unsigned stripe);
void experiment_write_enqueue(struct experiment_stats *stats, int recovery,
                              uint64_t same_channel, uint64_t all_channels);
void experiment_service(struct experiment_stats *stats, int recovery, unsigned operation,
                        uint64_t wait, struct experiment_request *owner);
/* Consumes the host-owned request record. Internal I/O only borrows its pointer. */
void experiment_host_complete(struct experiment_stats *stats, struct experiment_request *owner,
                              unsigned operation, int64_t arrival, int64_t response);
void experiment_borrow_window(struct experiment_stats *stats, uint64_t completed, uint64_t window,
                             int64_t now, unsigned parity, unsigned read, int proposed);
void experiment_borrow_commit(struct experiment_stats *stats, uint64_t completed, int64_t now,
                             unsigned parity, unsigned read);
void experiment_stats_finish(struct experiment_stats *stats);
void experiment_stats_destroy(struct experiment_stats *stats);
#endif
