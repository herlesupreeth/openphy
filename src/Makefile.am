AM_CFLAGS = -std=gnu99 $(UHD_CFLAGS)
AM_CPPFLAGS = $(all_includes)
AM_CXXFLAGS = $(UHD_CFLAGS) -std=c++14

DSP_LA = $(top_builddir)/src/dsp/libdsp.la
OPENPHY_LA = $(top_builddir)/src/openphy/libopenphy.la

SUBDIRS = dsp openphy

bin_PROGRAMS = ltedecode 

ltedecode_SOURCES = \
	BufferQueue.cpp \
	Converter.cpp \
	DecoderPDSCH.cpp \
	IOInterface.cpp \
	Resampler.cpp \
	SignalVector.cpp \
	Synchronizer.cpp \
	SynchronizerPBCH.cpp \
	SynchronizerPDSCH.cpp \
	TimestampBuffer.cpp \
	UHDDevice.cpp \
	FreqAverager.cpp \
	LteBuffer.cpp \
	main.cpp

ltedecode_LDADD = $(OPENPHY_LA) $(DSP_LA) $(FFTWF_LIBS) $(UHD_LIBS) $(OPENFEC_LIBS) -lboost_system
ltedecode_LDFLAGS = -pthread
