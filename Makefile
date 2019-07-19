CXX		?= g++
CXXFLAGS	?= -g -std=c++14 -fopenmp -Ofast -DNDEBUG -Werror
CPPFLAGS	?=
LDFLAGS		?= -fopenmp
CC		= $(CXX)

LIBLMDB		?= -llmdb

SOURCES = Makefile \
	  dumpdb.cc makedb.cc mergedb.cc scandb.cc subtrdb.cc \
	  lmdb++.h common.h config.h

SRCS = $(filter %.cc,$(SOURCES))
HDRS = $(filter %.hh,$(SOURCES)) $(filter %.h,$(SOURCES))
OBJS = $(SRCS:.cc=.o)
EXES = dumpdb makedb mergedb scandb subtrdb

.PHONY: all depend clean

all: $(EXES) depend

dumpdb:		$(LIBLMDB)
makedb:		$(LIBLMDB)
mergedb:	$(LIBLMDB)
scandb:		$(LIBLMDB)
subtrdb:	$(LIBLMDB)

depend: .depend
.depend: $(SRCS) config.h
	$(RM) $@
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -MM $^ > $@
include .depend

clean:
	$(RM) $(OBJS) $(EXES)
