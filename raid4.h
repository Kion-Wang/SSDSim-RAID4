#ifndef RAID4_H
#define RAID4_H
#include "ssd.h"
#include "parity_policy.h"
#define RAID4_XOR_FULL_PAGE_NS 19000u
/* Four members, four channels/member. Member 3 owns all parity pages. */
int raid4_prepare_outputs(struct ssd_info *ssd, int rounds, const struct parity_options *options);
FILE *raid4_discard_open(void);
FILE *raid4_result_open(const char *suffix, const char *mode);
int raid4_detailed_statistics(void);
void raid4_run(struct ssd_info *ssd, int rounds, const struct parity_options *options);
void raid4_recover(struct ssd_info *ssd, unsigned member);
void raid4_destroy(struct ssd_info *ssd);
int raid4_is_ppc(const struct ssd_info *ssd);
void raid4_note_service(struct ssd_info *ssd, struct sub_request *sub);
void raid4_preserve_old(struct ssd_info *ssd, unsigned lpn, unsigned ppn, unsigned state);
int raid4_gc_for_channel(struct ssd_info *ssd, unsigned channel);
void raid4_check_erase(struct ssd_info *ssd, unsigned channel, unsigned chip, unsigned die, unsigned plane, unsigned block);
struct sub_request *raid4_read_physical(struct ssd_info *ssd, struct request *io,
    unsigned stripe, unsigned lpn, unsigned ppn, unsigned mask);
#endif
