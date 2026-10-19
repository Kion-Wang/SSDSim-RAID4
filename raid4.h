#ifndef RAID4_H
#define RAID4_H
#include "ssd.h"
/* Four members, four channels/member. Member 3 owns all parity pages. */
void raid4_run(struct ssd_info *ssd, int rounds, int recovery_member);
void raid4_recover(struct ssd_info *ssd, unsigned member);
void raid4_destroy(struct ssd_info *ssd);
#endif
