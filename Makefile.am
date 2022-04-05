ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = tcpxfer
tcpxfer_SOURCES = \
    common.h \
    config.c \
    config.h \
    main.c \
    rate.c \
    rate.h \
    stats.c \
    stats.h

tcpxfer_CFLAGS = $(GSL_CFLAGS)
tcpxfer_LDFLAGS = -lm -lev $(GSL_LIBS)
