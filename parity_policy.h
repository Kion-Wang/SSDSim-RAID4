#ifndef PARITY_POLICY_H
#define PARITY_POLICY_H
#include <stdint.h>

enum parity_scheme { PARITY_BASELINE, PARITY_PPC, PARITY_PATCH, PARITY_BFP };
enum parity_commit_reason { COMMIT_REPLACEMENT, COMMIT_GC, COMMIT_EOF, COMMIT_SYNC, COMMIT_RESIZE, COMMIT_REASON_COUNT };
struct parity_options {
    enum parity_scheme scheme;
    int verify_data;
    int recovery_member;
    int64_t recover_after; /* -1: recover only after the final flush */
    int alternating_data; /* controlled two-version synthetic content, all schemes */
};
struct ppc_commit_plan {
    int use_old;
    unsigned read_mask[3];
    unsigned parity_mask;
    unsigned read_pages;
};
const char *parity_scheme_name(enum parity_scheme scheme);
int parity_options_parse(int argc, char **argv, struct parity_options *options);
struct ppc_commit_plan ppc_plan_commit(unsigned members, const unsigned current[3],
    const unsigned baseline[3], unsigned old_parity, int force_current);
#endif
