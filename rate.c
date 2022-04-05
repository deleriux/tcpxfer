#include "common.h"
#include "rate.h"
#include "config.h"
#include <arpa/inet.h>

struct ev_loop *loop = NULL;
double last_epoch = 0;
uint64_t received_bytes = 0;
double latency_total = 0.0;

static int tcp_listener(char *port);
static int tcp_connect(char *host, char *port);

static void rate_connect(EV_P_ ev_io *w, int revents);
static void rate_sendrecv(EV_P_ ev_io *w, int revents);
static void rate_relisten(void);
static void rate_reconnect(void);

static void connect_timeout(EV_P_ ev_timer *t, int revents);
static void pps_limit(EV_P_ ev_io *tfd, int revents);


struct rate_data {
  int sfd;
  int fd;
  int tfd;
  ev_io w;
  ev_timer t;
  ev_io tfdw;
  struct configuration *c;
  uint64_t runs;
  bool ready;
} rate;



static void dbl_to_ts(
    double tm, struct timespec *ts)
{
  double i, f;
  f = modf(tm, &i);
  ts->tv_sec = i;
  ts->tv_nsec = lround(f * BILLION);
}


static void timerfd_stop(
    void)
{
  ev_io_stop(EV_DEFAULT_ &rate.tfdw);
  close(rate.tfd);
  rate.tfd = -1;
}

static void timerfd_start(
    void)
{
  struct itimerspec its;
  rate.tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);
  if (rate.tfd < 0)
    err(EXIT_FAILURE, "timerfd_create");

  dbl_to_ts(rate.c->per_packet_wait, &its.it_interval);
  dbl_to_ts(rate.c->per_packet_wait, &its.it_value);

  if (timerfd_settime(rate.tfd, 0, &its, NULL) < 0)
    err(EXIT_FAILURE, "tiemrfd_settime");

  ev_io_stop(EV_DEFAULT_ &rate.tfdw);
  ev_io_set(&rate.tfdw, rate.tfd, EV_READ);
  ev_set_cb(&rate.tfdw, pps_limit);
  ev_io_start(EV_DEFAULT_ &rate.tfdw);
}

static int tcp_listener(
    char *port)
{
  struct addrinfo *ai, hints;
  int fd, rc;

  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  rc = getaddrinfo(NULL, port, &hints, &ai);
  if (rc)
    errx(EXIT_FAILURE, "Unable to listen: %s", gai_strerror(rc));

  fd = socket(ai->ai_family, ai->ai_socktype|SOCK_NONBLOCK|SOCK_CLOEXEC, ai->ai_protocol);
  if (fd < 0)
    err(EXIT_FAILURE, "socket()");

  rc = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &rc, sizeof(rc)) < 0)
    err(EXIT_FAILURE, "setsockopt()");

  if (bind(fd, ai->ai_addr, ai->ai_addrlen) < 0)
    err(EXIT_FAILURE, "Unable to listen");

  if (listen(fd, 1) < 0)
    err(EXIT_FAILURE, "Unable to listen");

  freeaddrinfo(ai);
  return fd;
}



static int tcp_connect(
    char *host,
    char *port)
{
  struct addrinfo *ai = NULL, hints;
  int fd = -1;
  int rc;

  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = 0;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  rc = getaddrinfo(host, port, &hints, &ai);
  if (rc) {
    warnx("Coudln't resolve hostname: %s", gai_strerror(rc));
    goto fail;
  }

  fd = socket(ai->ai_family, ai->ai_socktype|SOCK_NONBLOCK|SOCK_CLOEXEC, ai->ai_protocol);
  if (fd < 0)
    goto fail;

  if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
    if (errno == EINPROGRESS)
      rc = EINPROGRESS;
    else {
      usleep(250000);
      goto fail;
    }
  }

  freeaddrinfo(ai);
  errno = EINPROGRESS;
  return fd;

fail:
  if (ai)
    freeaddrinfo(ai);
  if (fd > -1)
    close(fd);
  return -1;
}



static void connect_timeout(
    EV_P_ ev_timer *t,
    int revents)
{
  struct rate_data *r = t->data;

  warnx("Connection to host timed out");
  rate_reconnect();
}



static void pps_limit(
    EV_P_ ev_io *t,
    int revents)
{
  struct rate_data *r = t->data;
  int rc;
  uint64_t overs;
  rc = read(rate.tfd, &overs, sizeof(overs));
  if (rc < 0) {
    if (errno == EAGAIN)
      return;
    else
      err(EXIT_FAILURE, "timerfd()->read()");
  }

  if ((r->w.events & EV_WRITE)) {
    /* If there is no write pending, but you are looking for writes,
     * then the send buffer must be full. We dont want to log our overruns
     * in this situation as it will cause a 'burst' later otherwise 
    */
    if (!ev_is_pending(&r->w)) {
      return;
    }
  }
  else {
    ev_io_set(&r->w, r->fd, EV_READ|EV_WRITE);
    ev_io_stop(EV_A_ &r->w);
    ev_io_start(EV_A_ &r->w);
  }

  rate.runs += (overs+1);
}



static void rate_listen(
    EV_P_ ev_io *w,
    int revents)
{
  struct rate_data *r = w->data;
  struct sockaddr addr;
  socklen_t len = sizeof(addr);
  char h[64];
  memset(h, 0, sizeof(h));
  
  r->fd = accept4(r->sfd, &addr, &len, SOCK_NONBLOCK);
  if (r->fd < 0) {
    warn("Cannot accept new connection");
    return;
  }
  rate.ready = true;
  received_bytes = 0;
  if (rate.c->hostname)
    free(rate.c->hostname);
  getnameinfo(&addr, len, h, 128, NULL, 0, 0);
  rate.c->hostname = strdup(h);

  ev_io_stop(EV_A_ w);

  ev_io_set(w, r->fd, EV_WRITE|EV_READ);
  ev_set_cb(w, rate_sendrecv);
  ev_io_start(EV_A_ w);

  timerfd_start();

}



static void rate_connect(
    EV_P_ ev_io *w,
    int revents)
{
  int rc = sizeof(int);
  int eno;
  struct rate_data *r = w->data;

  rc = getsockopt(r->fd, SOL_SOCKET, SO_ERROR, &eno, &rc);
  if (rc < 0) 
    warn("connect()->getsockopt()");

  if (eno == EINPROGRESS)
    return;
  else if (eno > 0) {
    errno = eno;
    usleep(250000);
    rate_reconnect();
  }
  else {
    rate.ready = true;
    received_bytes = 0;
    ev_timer_stop(EV_A_ &r->t);
    ev_io_set(w, rate.fd, EV_READ|EV_WRITE);
    ev_set_cb(w, rate_sendrecv);
    ev_io_stop(EV_A_ w);
    ev_io_start(EV_A_ w);
    timerfd_start();
  }
}



static void rate_recv(
    void)
{
  int rc;
  uint8_t buffer[DATA_SZ];
  uint64_t total = 0;

  while (1) {
    rc = recv(rate.fd, buffer, DATA_SZ, 0);
    if (rc < 0) {
      if (errno == EAGAIN)
        break;
    }
    else if (rc == 0) {
      return;
    }
    else {
      total += rc;
    }
  }
}



static void rate_send(
    void)
{
  int rc;
  uint8_t buffer[DATA_SZ]; /* Care so little whats in here */
  uint64_t total = 0;

  while (rate.runs-- > 0) {
    rc = send(rate.fd, buffer, DATA_SZ, MSG_NOSIGNAL);
    if (rc < 0) {
      if (errno == EPIPE) {
        if (rate.c->listener) {
          warn("Send failed");
          rate_relisten();
        }
        else {
          rate_reconnect();
        }
        return;
      }
      else if (errno == EAGAIN) {
        ev_io_set(&rate.w, rate.fd, EV_READ|EV_WRITE);
        ev_io_stop(EV_DEFAULT_ &rate.w);
        ev_io_start(EV_DEFAULT_ &rate.w);
        goto out;
      }
      else {
        warn("Send failed");
      }
    }
    total += rc;
  }

  if (rate.w.events & EV_WRITE) {
    ev_io_set(&rate.w, rate.fd, EV_READ);
    ev_io_stop(EV_DEFAULT_ &rate.w);
    ev_io_start(EV_DEFAULT_ &rate.w);
  }

out:
  return;
}


static void rate_sendrecv(
    EV_P_ ev_io *w,
    int revents)
{
  if (revents & EV_READ) rate_recv();
  if (revents & EV_WRITE) rate_send();
}


static void rate_relisten(
    void)
{
  close(rate.fd);
  rate.fd = -1;
  rate.ready = false;
  ev_io_stop(EV_DEFAULT_ &rate.w);
  ev_io_set(&rate.w, rate.sfd, EV_READ);
  ev_set_cb(&rate.w, rate_listen);
  ev_io_start(EV_DEFAULT_ &rate.w);
  timerfd_stop();
}



static void rate_reconnect(
    void)
{
  close(rate.fd);
  rate.fd = -1;
  ev_io_stop(EV_DEFAULT_ &rate.w);
  ev_timer_stop(EV_DEFAULT_ &rate.t);

  rate.ready = false;
  rate.fd = tcp_connect(rate.c->hostname, rate.c->port);
  if (errno == EINPROGRESS) {
    ev_init(&rate.w, rate_connect);
    ev_init(&rate.t, connect_timeout);
    ev_io_set(&rate.w, rate.fd, EV_WRITE);
    ev_timer_set(&rate.t, CONNECT_TIMEOUT, 0.);
    ev_timer_start(EV_DEFAULT_ &rate.t);
  }
  else if (errno == 0) {
    rate.ready = true;
    received_bytes = 0;
    ev_init(&rate.w, rate_sendrecv);
    ev_init(&rate.tfdw, pps_limit);
    timerfd_start();
    ev_io_set(&rate.w, rate.fd, EV_READ|EV_WRITE);
  }

  ev_io_start(EV_DEFAULT_ &rate.w);
}


void rate_stop(
    void)
{
  ev_timer_stop(EV_DEFAULT_ &rate.t);
  ev_io_stop(EV_DEFAULT_ &rate.w);
  close(rate.fd);
}



void rate_listener(
    void)
{
  rate.ready = false;
  received_bytes = 0;
  rate.c = config_get();
  rate.fd = -1;
  rate.sfd = tcp_listener(rate.c->port);
  if (rate.sfd < 0)
    err(EXIT_FAILURE, "Cannot listen on port");
  ev_init(&rate.w, rate_listen);
  ev_init(&rate.tfdw, pps_limit);
  rate.t.data = &rate;
  rate.w.data = &rate;
  rate.tfdw.data = &rate;
  ev_io_set(&rate.w, rate.sfd, EV_READ);
  last_epoch = ev_now(EV_DEFAULT);
  ev_io_start(EV_DEFAULT_ &rate.w);
}



void rate_connector(
    void)
{

  rate.c = config_get();
  rate.fd = tcp_connect(rate.c->hostname, rate.c->port);
  if (rate.fd < 0)
    exit(EXIT_FAILURE);

  rate.ready = false;
  received_bytes = 0;
  rate.t.data = &rate;
  rate.w.data = &rate; 
  rate.tfdw.data = &rate;
  ev_init(&rate.tfdw, pps_limit);
  /* Try to always check the timer before the socket */
  ev_set_priority(&rate.tfdw, 1);
  if (errno == EINPROGRESS) {
    ev_init(&rate.w, rate_connect);
    ev_init(&rate.t, connect_timeout);
    ev_io_set(&rate.w, rate.fd, EV_WRITE);
    ev_timer_set(&rate.t, CONNECT_TIMEOUT, 0.);
    ev_timer_start(EV_DEFAULT_ &rate.t);
  }
  else if (errno == 0) {
    rate.ready = true;
    received_bytes = 0;
    ev_init(&rate.w, rate_sendrecv);
    ev_init(&rate.tfdw, pps_limit);
    timerfd_start();
    ev_io_set(&rate.w, rate.fd, EV_READ|EV_WRITE);
  }
  rate.t.data = &rate;
  rate.w.data = &rate;
  last_epoch = ev_now(EV_DEFAULT);
  ev_io_start(EV_DEFAULT_ &rate.w);
}



int rate_update_stats(
    stat_record_t *s)
{
  double now = ev_now(EV_DEFAULT);
  uint64_t bps;
  uint64_t total;
  struct tcp_info tcpi;
  int tcpisz = sizeof(tcpi);

  now = ev_now(EV_DEFAULT_UC);

  if (rate.fd < 0 || !rate.ready) {
    return 0;
  }
  else if (rate.ready) {
    if (getsockopt(rate.fd, IPPROTO_TCP, TCP_INFO, &tcpi, &tcpisz) < 0) {
      if (errno == EBADF) 
        return 0;
      else
        err(EXIT_FAILURE, "getsockopt");
    }
  }

  bps = (tcpi.tcpi_bytes_received - received_bytes) / (now - last_epoch);
  latency_total += tcpi.tcpi_rtt;
  s->timestamp = now;
  s->bps = bps;
  s->bytes_total = tcpi.tcpi_bytes_received;
  s->latency_us = tcpi.tcpi_rtt;
  s->latency_total = latency_total;

  received_bytes = tcpi.tcpi_bytes_received;
  last_epoch = now;
  return 1;
}
