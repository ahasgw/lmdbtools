CXX		?= g++
CXXFLAGS	?= -g -std=c++14 -fopenmp -Ofast -DNDEBUG -Werror
CPPFLAGS	?= -I/usr/include/eigen3
LDFLAGS		?= -fopenmp
CC		= $(CXX)

SOURCES = Makefile \
	  dumpdb.cc scandb.cc makesmidb.cc maketfmdb.cc mergedb.cc subtrdb.cc \
	  chemstgen.cc fwd2revdb.cc smi2can.cc smi2svg.cc \
	  lmdb++.h chemstgen.h crypto_hash.h \
	  config.h

SRCS = $(filter %.cc,$(SOURCES))
HDRS = $(filter %.hh,$(SOURCES)) $(filter %.h,$(SOURCES))
OBJS = $(SRCS:.cc=.o)
EXES = dumpdb scandb makesmidb maketfmdb mergedb subtrdb \
       chemstgen fwd2revdb smi2can smi2svg

.PHONY: all depend clean

all: $(EXES) depend

dumpdb:    -llmdb
scandb:    -llmdb
makesmidb: -llmdb -lhelium -lcrypto
maketfmdb: -llmdb -lhelium
mergedb:   -llmdb
subtrdb:   -llmdb
chemstgen: -llmdb -lhelium -lcrypto
fwd2revdb: -llmdb
smi2can:   -lhelium -lcrypto
smi2svg:   -lhelium -lcrypto

depend: .depend
.depend: $(SRCS) config.h
	$(RM) $@
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -MM $^ > $@
include .depend

clean:
	$(RM) $(OBJS) $(EXES)
