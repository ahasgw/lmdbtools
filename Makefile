CXX		?= g++
CXXFLAGS	?= -g -std=c++14 -Ofast -DNDEBUG -Werror -Wextra
CPPFLAGS	?=
LDFLAGS		?=
CC		= $(CXX)

LIBLMDB		?= -llmdb

SOURCES = Makefile \
	  adddb.cc dumpdb.cc makedb.cc mergedb.cc scandb.cc subtrdb.cc \
	  lmdb++.h

SRCS = $(filter %.cc,$(SOURCES))
HDRS = $(filter %.hh,$(SOURCES)) $(filter %.h,$(SOURCES))
OBJS = $(SRCS:.cc=.o)
EXES = adddb dumpdb makedb mergedb scandb subtrdb

.PHONY: all depend clean

all: $(EXES) depend

adddb:		$(LIBLMDB)
dumpdb:		$(LIBLMDB)
makedb:		$(LIBLMDB)
mergedb:	$(LIBLMDB)
scandb:		$(LIBLMDB)
subtrdb:	$(LIBLMDB)

depend: .depend
.depend: $(SRCS)
	$(RM) $@
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -MM $^ > $@
include .depend

clean:
	$(RM) $(OBJS) $(EXES)
