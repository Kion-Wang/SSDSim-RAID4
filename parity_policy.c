#include "parity_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

const char *parity_scheme_name(enum parity_scheme scheme) {
    switch (scheme) {
    case PARITY_BASELINE: return "baseline";
    case PARITY_PPC: return "ppc";
    case PARITY_PATCH: return "patch";
    case PARITY_BFP: return "bfp";
    default: return "invalid";
    }
}
static int number(const char *s, int64_t *out) {
    char *end;
    errno = 0;
    long long n = strtoll(s, &end, 10);
    if (errno || !*s || *end || n < 0) return 0;
    *out = n; return 1;
}
int parity_options_parse(int argc, char **argv, struct parity_options *o) {
    *o = (struct parity_options){PARITY_BASELINE, 0, -1, -1, 0};
    int saw_scheme = 0, saw_pattern = 0;
    for (int i = 3; i < argc; ++i) {
        const char *arg = argv[i], *value = NULL;
        if (!strcmp(arg, "--verify-data")) { o->verify_data = 1; continue; }
        if (!strcmp(arg, "--data-pattern")) {
            if (++saw_pattern > 1 || i + 1 == argc) return 0;
            const char *pattern = argv[++i];
            if (!strcmp(pattern,"versioned")) o->alternating_data=0;
            else if (!strcmp(pattern,"alternating")) o->alternating_data=1;
            else { fprintf(stderr,"Unknown data pattern: %s\n",pattern); return 0; }
            continue;
        }
        if (!strncmp(arg, "--scheme=", 9)) value = arg + 9;
        else if (!strcmp(arg, "--scheme") && i + 1 < argc) value = argv[++i];
        if (value) {
            if (saw_scheme++) { fprintf(stderr, "Specify --scheme once.\n"); return 0; }
            if (!strcmp(value, "baseline")) o->scheme = PARITY_BASELINE;
            else if (!strcmp(value, "ppc")) o->scheme = PARITY_PPC;
            else if (!strcmp(value, "patch")) o->scheme = PARITY_PATCH;
            else if (!strcmp(value, "bfp")) o->scheme = PARITY_BFP;
            else { fprintf(stderr, "Unsupported scheme '%s': available schemes are baseline, ppc, patch and bfp.\n", value); return 0; }
            continue;
        }
        int after = !strcmp(arg, "--recover-after");
        int member = !strcmp(arg, "--recover-member");
        int64_t n;
        if (after || member) {
            if (i + 1 == argc || !number(argv[++i], &n)) return 0;
            if (after) { if (o->recover_after >= 0) return 0; o->recover_after = n; }
            else { if (o->recovery_member >= 0 || n > 3) return 0; o->recovery_member = (int)n; }
        } else if (o->recovery_member < 0 && number(arg, &n) && n <= 3) o->recovery_member = (int)n;
        else { fprintf(stderr, "Unknown or invalid option: %s\n", arg); return 0; }
    }
    if (o->recover_after >= 0 && o->recovery_member < 0) {
        fprintf(stderr, "--recover-after requires --recover-member (0..3).\n"); return 0;
    }
    return 1;
}
struct ppc_commit_plan ppc_plan_commit(unsigned members, const unsigned current[3],
    const unsigned baseline[3], unsigned old_parity, int force_current) {
    struct ppc_commit_plan p = {0, {0,0,0}, 0, 0};
    unsigned old_cost = old_parity != 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!(members & (1u << i))) p.read_pages += current[i] != 0;
        else old_cost += baseline[i] != 0;
    }
    /* On a tie, exercise the old-parity path when a committed parity exists.
     * Missing pages are logical zeros and cause no NAND I/O. */
    p.use_old = !force_current && old_parity && old_cost <= p.read_pages;
    for (unsigned i = 0; i < 3; ++i)
        p.read_mask[i] = p.use_old ? ((members & (1u << i)) ? baseline[i] : 0)
                                 : ((members & (1u << i)) ? 0 : current[i]);
    if (p.use_old) { p.parity_mask = old_parity; p.read_pages = old_cost; }
    return p;
}
