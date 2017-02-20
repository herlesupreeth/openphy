AM_CFLAGS = -Wall -std=c99 -fvisibility=hidden $(OPENFEC_CFLAGS) $(UHD_CFLAGS) -pthread -I$(top_srcdir)/src/dsp $(WIRESHARK_CFLAGS)  -march=native
AM_CPPFLAGS = $(UHD_CFLAGS)

noinst_LTLIBRARIES = libopenphy.la

libopenphy_la_SOURCES = \
	lte.cc \
	pss.c \
	sss.c \
	ref.c \
	crc.c \
	pbch.c \
	pbch_block.c \
	pcfich.c \
	phich.c \
	pdcch.c \
	pdcch_block.c \
	pdcch_interleave.c \
	qam.c \
	precode.c \
	ofdm.c \
	slot.c \
	gold.c \
	sync.c \
	sync_pss.c \
	dci.c \
	dci_formats.c \
	scramble.c \
	expand.c \
	pdsch.c \
	pdsch_vrb.c \
	pdsch_riv.c \
	pdsch_tbs.c \
	pdsch_block.c \
	log.c \
	wireshark.c \
	socket.c
