#ifndef _CONFIG_H_
#define _CONFIG_H_
#include "common.h"

struct configuration {
  int fd;
  double print_interval;
  int64_t rate_per_second;
  double per_packet_wait;
  char *port;
  char *hostname;
  bool listener;
};

void config_parse(int argc, char **argv);
struct configuration * config_get(void);
#endif
