#include "common.h"
#include "config.h"
#include <getopt.h>

struct configuration config;

static inline void print_usage(
    void)
{
  printf("Usage: tcpxfer [OPTIONS] (-l) hostname\n");
}

static inline void print_help(
    void)
{
  printf(
"Perform a consistent TCP data transfer to another host and record the results to stdout.\n\n"
"OPTIONS\n"
"    --help                -h           Print this help\n"
"    --listen              -l           Whether to be a listener instead. <hostname> is ignored.\n"
"    --port                -p PORT      Use port PORT. Default: %s\n"
"    --interval            -i INTERVAL  Print result data in INTERVAL seconds. Default 10 seconds.\n"
"    --rate                -r           Ceiling of transfer rate. Default 1mbps.\n"
"\n", DEFAULT_PORT);
}

void config_parse(
    int argc,
    char **argv)
{
  char c;
  int optidx;

  double tmpdbl;
  char *p;
  char rate;
  int rc;

  memset(&config, 0, sizeof(config));

  static struct option long_options[] = {
    { "help",        no_argument,       NULL, 'h' },
    { "listen",      no_argument,       NULL, 'l' },
    { "rate",        required_argument, NULL, 'r' },
    { "interval",    required_argument, NULL, 'i' },
    { "port",        required_argument, NULL, 'p' },
    {  0,            0,                 0,     0  },
  };

  config.listener = 0;
  config.fd = -1;
  config.print_interval = 10.0;
  config.rate_per_second = DEFAULT_RATE_PER_SEC;
  config.port = NULL;
  config.hostname = NULL;
  config.per_packet_wait = 0.0;

  while (1) {
    c = getopt_long(argc, argv, "hlr:i:p:", long_options, &optidx);
    if (c == -1)
      break;

    switch(c) {

    case 'h':
      print_usage();
      print_help();
      exit(1);
    break;

    case 'r':
      rc = sscanf(optarg, "%lf%cbps", &tmpdbl, &rate);
      if (rc != 2) {
        err(EXIT_FAILURE, "Rate must be a valid value. But %s was offered.", optarg);
      }
      switch (rate) {
        case 'g':
          tmpdbl *= 1024;
        case 'm':
          tmpdbl *= 1024;
        case 'k':
          tmpdbl *= 1024;
        case 'b':
          config.rate_per_second = lroundf(tmpdbl);
        break;

        default:
          errx(EXIT_FAILURE, "Rate must be in bps, kbps, mbps, gbps, but was %s", optarg);
        break;
      }
      if (config.rate_per_second < 1 || config.rate_per_second >= INT32_MAX)
        errx(EXIT_FAILURE, "Size must be between %llubps and %llubps but was %s", 1, INT32_MAX, optarg);
    break;

    case 'p':
      config.port = strdup(optarg);
      assert(config.port);
    break;

    case 'i':
      errno = 0;
      tmpdbl = strtod(optarg, &p);
      if (strlen(optarg) != p-optarg || errno == ERANGE)
        errx(EXIT_FAILURE, "Interval must be between 0.01 and 86400 seconds, not %s", optarg);

      if (tmpdbl < 0.01 || tmpdbl > 86400.0)
        errx(EXIT_FAILURE, "Interval must be between 0.01 and 86400 seconds, not %s", optarg);
      config.print_interval = tmpdbl;
    break;

    case 'l':
      config.listener = true;
    break;

    default:
      print_usage();
      print_help();
      exit(1);
    break;
    }
  }

  if (argv[optind] == NULL && config.listener == false) 
    errx(EXIT_FAILURE, "If not listening, must pass a host to connect to.");

  if (!config.port)
    config.port = strdup(DEFAULT_PORT);

  if (config.listener) 
    config.hostname = NULL;
  else {
    config.hostname = strdup(argv[optind]);
    assert(config.hostname);
  }

  assert(config.port);

  config.per_packet_wait = (double)config.rate_per_second / (double)DATA_SZ;
  config.per_packet_wait = 1.0 / config.per_packet_wait;

}

struct configuration * config_get(
    void)
{
  return &config;
}
