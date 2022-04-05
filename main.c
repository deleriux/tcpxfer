#include "common.h"
#include "config.h"
#include "rate.h"

bool running = true;
struct ev_loop * loop;

struct statistics *stats;

/*
void print_stats(
    EV_P_ ev_timer *t,
    int revents)
{
  ev_tstamp now;
  int fd = rate_get_fd();
  struct tcp_info tcpi;
  int tcpisz = sizeof(tcpi);
  struct configuration *c = config_get();

  memset(&tcpi, 0, sizeof(tcpi));

  now = ev_now(EV_DEFAULT_UC);

//  packets = s->packets;
//  s->packets = 0;
//  s->total += packets;
//  s->last_epoch_time = epoch;

  stats->run_time += (now-stats->last_epoch_time);
  stats->last_epoch_time = now;

  if (fd > -1) {
    if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &tcpi, &tcpisz) < 0) {
      if (errno == EBADF) 
        return;
      else
        err(EXIT_FAILURE, "getsockopt");
    }
  }

  stats->in_rtt = tcpi.tcpi_rcv_rtt;
  stats->out_rtt = tcpi.tcpi_rtt;
  stats->out_rtt_dev = tcpi.tcpi_rttvar;

  printf("%10.6f\t"
          "%03.6f\t"

          "%12llu\t"
          "%08.3fKb/s\t"

          "%12llu\t"
          "%08.3fKb/s\t"
          "%6.3fms\t"

          "+/-%1.3fms\t"
          "\n",

          stats->last_epoch_time,
          stats->run_time,

          stats->in_interval_data, 
          (stats->in_interval_data/c->print_interval) / 1024, 

          stats->out_interval_data, 
          (stats->out_interval_data/c->print_interval) / 1024, 
          stats->out_rtt/1000,
          sqrt(stats->out_rtt_dev)/1000
  );

  stats->in_total += stats->in_interval_data;
  stats->in_interval_data = 0;
  stats->out_total += stats->out_interval_data;
  stats->out_interval_data = 0;
  fflush(stdout);
}
*/
                                                                                                         
int main(
    int argc,
    char **argv) 
{
  ev_timer t;

  if (!ev_default_loop(0))
    errx(EXIT_FAILURE, "could not initialise libev, bad $LIBEV_FLAGS in environment?");

  config_parse(argc, argv);
  struct configuration *config = config_get();

  stats_init(config->rate_per_second, rate_update_stats);

  if (config->listener)
    rate_listener();
  else {
    rate_connector();
  }

  ev_run(EV_DEFAULT_ 0);

  exit(0);
}
