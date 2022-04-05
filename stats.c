#include "common.h"
#include "config.h"
#include "stats.h"
#include <gsl/gsl_statistics.h>

#define SAMPLE_SZ 15
#define WATERMARK_LATENCY_LO    .95
#define WATERMARK_LATENCY_HI    .99
#define WATERMARK_THROUGHPUT_LO .95
#define WATERMARK_THROUGHPUT_HI .99


#define UNCHANGED       0x0
#define LATENCY_OK      0x1
#define LATENCY_CRIT    0x2
#define THROUGHPUT_OK   0x4
#define THROUGHPUT_CRIT 0x8

char stampstr[64];

static char * link_latency_str(void);
static char * link_throughput_str(void);


static struct {
  ev_timer timer;
  bool disconnected;

  int nrecs;
  int nextrec;
  stat_record_t *records;

  int (*stats_record_cb)(stat_record_t *);
  double rate;
  double throughput_fitness;
  double latency_fitness;

  double throughput_mean;
  double latency_mean;
  int state;
  bool alerting;
} records;


static char *strstamp(
    double stamp)
{
  double rem, nil;
  char str[64];

  long st = lround(stamp);
  struct tm *time;
  rem = modf(stamp, &nil);
  rem *= 1000;
  time = localtime(&st);
  strftime(str, 63, "%Y-%m-%d %H:%M:%S", time);
  snprintf(stampstr, 63, "%s.%03.0f", str, rem);
  return stampstr;
}



static void print_lines(
    int lines,
    int nsamples)
{
  int i, j;
  stat_record_t *r;
  int rnum;
  char hostport[26];
  struct configuration *c = config_get();
  double *timebin = alloca(sizeof(double) * nsamples);
  double *latebin = alloca(sizeof(double) * nsamples);
  double *bpsbin = alloca(sizeof(double) * nsamples);

  stat_record_t *meanrecs = alloca(sizeof(stat_record_t) * lines);
  stat_record_t *t;

  rnum = (records.nextrec-(lines * nsamples)) % records.nrecs;
  snprintf(hostport, 25, "%s:%s", c->hostname, c->port);

  for (i=0; i < lines; i++) {
    /* For each line */
    t = &meanrecs[i];
    /* Timestamps */
    for (j=0; j < nsamples; j++) {
      r = &records.records[rnum++];
      timebin[j] = r->timestamp;
      latebin[j] = r->latency_us;
      bpsbin[j] = r->bps;
      if (r->state != LINK_UNCHANGED) /* Obtains the 'max' state */
        t->state = r->state;
    }
    /* Resample and assign to our temp record */
    t->timestamp = gsl_stats_max(timebin, 1, nsamples);
    t->latency_us = gsl_stats_mean(latebin, 1, nsamples);
    t->bps = gsl_stats_mean(bpsbin, 1, nsamples);
  }

  /* Print the output now of each record */
  for (i=0; i < lines; i++) {
    t = &meanrecs[i];
    if (t->timestamp < 100 || isnan(t->timestamp))
      continue;

    printf("%s %s %.3fkbps %.3fms", strstamp(t->timestamp),
                             hostport,
                             t->bps/1024,
                             t->latency_us/1000);
    if (records.disconnected && t->state == LINK_CONNECTED)
      printf(" Connection established.");
    else if (!records.disconnected && t->state == LINK_DISCONNECTED)
      printf(" Connection has been lost.");
    printf("\n");
  }
  fflush(stdout);

}


static void print_stats(
    void)
{
  printf("\nAverage Throughput: %.3fkbps\nAverage Latency:  %.3fms\nConnection Quality: %.1f%%\n"
         "Status: %s (%.2f) | %s (%.2f). Alert mode: %s\n"
         "\n",
    records.throughput_mean/1024, records.latency_mean/1000,
    (records.latency_fitness + records.throughput_fitness) * 50.0,
    link_latency_str(), records.latency_fitness,
    link_throughput_str(), records.throughput_fitness,
    records.alerting ? "ON" : "OFF");
  fflush(stdout);
}


static int link_state(
    void)
{
  int state=0;
  int state_old = records.state;

  double rcl = records.latency_fitness;
  double tp = records.throughput_fitness;

  if (isnan(rcl))
    state |= LATENCY_CRIT;
  else if (rcl < WATERMARK_LATENCY_LO)
    state |= LATENCY_CRIT;
  else 
    state |= LATENCY_OK;

  if (isnan(tp))
    state |= THROUGHPUT_CRIT;
  else if (tp < WATERMARK_THROUGHPUT_LO)
    state |= THROUGHPUT_CRIT;
  else 
    state |= THROUGHPUT_OK;

  if (state & (LATENCY_OK|THROUGHPUT_OK) == (LATENCY_OK|THROUGHPUT_OK))
    records.alerting = false;
  if (state & (LATENCY_CRIT|THROUGHPUT_CRIT))
    records.alerting = true;

  records.state = state;
  return state == state_old ? 0 : state;
}


static char * link_latency_str(
    void)
{
  int state = records.state;
  if (state & LATENCY_CRIT)
    return "Latency quality is critical";
  else if (state & LATENCY_OK)
    return "Latency quality is good.";
  else
    return "Latency quality is unchanged";
}

static char * link_throughput_str(
    void)
{
  int state = records.state;
  if (state & THROUGHPUT_CRIT)
    return "Throughput quality is critical";
  else if (state & THROUGHPUT_OK)
    return "Throughput quality is good";
  else
    return "Throughput quality is unchanged";
}


static bool link_was_disconnected(
    void)
{
  int i;
  for (i=0; i < records.nrecs; i++) {
    if (records.records[i].state == LINK_DISCONNECTED) {
      return true;
    }
  }
  return false;
}

static void stats_fitness(
    void)
{
  int recno, i;
  stat_record_t *r;
  double *throug_vec = alloca(sizeof(double) * records.nrecs);
  double *latenc_vec = alloca(sizeof(double) * records.nrecs);
  double *timest_vec = alloca(sizeof(double) * records.nrecs);
  double *thrtot_vec = alloca(sizeof(double) * records.nrecs);
  double *lattot_vec = alloca(sizeof(double) * records.nrecs);

  /* Extract the stats as plain vectors */  
  for (i=0; i < records.nrecs; i++) {
    recno = (records.nextrec+i) % records.nrecs;
    r = &records.records[recno];
    timest_vec[i] = r->timestamp;
    thrtot_vec[i] = r->bytes_total;
    lattot_vec[i] = r->latency_total;
    throug_vec[i] = r->bps;
    latenc_vec[i] = r->latency_us;
  }
  records.throughput_fitness = 
    gsl_stats_correlation(timest_vec, 1, thrtot_vec, 1, records.nrecs);
  records.latency_fitness = 
    gsl_stats_correlation(timest_vec, 1, lattot_vec, 1, records.nrecs);
  records.latency_mean = gsl_stats_mean(latenc_vec, 1, records.nrecs);
  records.throughput_mean = gsl_stats_mean(throug_vec, 1, records.nrecs);
  return;
}


static void timer_fired(
    EV_P_ ev_timer *t,
    int revents)
{

  int rc;

  /* Allocate the next record in the log */
  stat_record_t *r;
  r = &records.records[records.nextrec % records.nrecs];
  r->_epoch++;

  /* The record did not update */
  rc = records.stats_record_cb(r);
  if (rc == 0) {
    if (!records.disconnected) {
      r->state = LINK_DISCONNECTED;
      print_lines(5, 5);
      print_stats();
    }
    else {
      r->state = LINK_UNCHANGED;
    }
    records.disconnected = true;
  }
  else {
    if (records.disconnected) {
      r->state = LINK_CONNECTED;
      print_lines(1, 5);
      records.disconnected = false;
    }
    else {
      r->state = LINK_UNCHANGED;
    }
  }

  records.nextrec++;
  stats_fitness();

  /* Perform a quality check */
  if (!link_was_disconnected()) {
    /* If state has changed from previous */
    if (link_state()) {
      print_lines(5, 5);
      print_stats();
    }
    /* If state hasn't changed but we're now alerting */
    if (records.alerting && (records.nextrec % 25 == 0)) /* five seconds */ {
      print_lines(5, 5);
    }
    if (records.alerting && (records.nextrec % 150) == 0) /* Thirty seconds */ {
      print_stats();
    }
  }

  return;
}


void stats_init(
    int64_t rate,
    int (*stat_cb)(stat_record_t *))
{
  assert(rate > 0);
  assert(stat_cb);

  ev_timer_init(&records.timer, timer_fired, STATS_FREQUENCY, STATS_FREQUENCY);

  records.state = 0;
  records.latency_fitness = 0.;
  records.throughput_fitness = 0.;
  records.disconnected = true;
  records.rate = (double)rate;
  records.nrecs = NRECORDS;
  records.nextrec = 0;
  records.stats_record_cb = stat_cb;
  records.records = calloc(sizeof(stat_record_t), NRECORDS);
  assert(records.records);

  ev_timer_start(EV_DEFAULT_ &records.timer);
}
