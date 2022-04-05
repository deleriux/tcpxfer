#ifndef _COMMON_H_
#define _COMMON_H_
#define _GNU_SOURCE

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <err.h>
#include <time.h>
#include <netdb.h>
#include <signal.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <linux/tcp.h>
#include <assert.h>

#define MILLION 1000000
#define BILLION 1000000000
#define DATA_SZ 1024
#define DEFAULT_PORT "8580"
#define DEFAULT_RATE_PER_SEC 1048576
#define CONNECT_TIMEOUT 5.0

#define EV_STANDALONE 1
#include "ev.h"


#endif
