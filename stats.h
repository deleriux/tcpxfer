#ifndef _STATS_H_
#define _STATS_H_

#define STATS_FREQUENCY 0.2F
#define STATS_SECS 15L
#define NRECORDS (int) (STATS_SECS / STATS_FREQUENCY)

typedef enum state {
  LINK_UNCHANGED,
  LINK_DISCONNECTED, /* Total link failure */
  LINK_CONNECTED
} stat_state_t;

typedef struct stat_record {
  double timestamp;
  double bps;
  double latency_us;
  double latency_total;
  double bytes_total;

  int _epoch;
  stat_state_t state;
} stat_record_t;

void stats_init(int64_t rbps, int (*cb)(stat_record_t *s));
#endif 
