#ifndef _RATE_H_
#define _RATE_H_
#include "stats.h"

void rate_listener(void);
void rate_connector(void);
void rate_stop(void);
int rate_update_stats(stat_record_t *s);

#endif
