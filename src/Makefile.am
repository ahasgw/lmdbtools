bin_PROGRAMS = makesmidb maketfmdb dumpdb chemstgen chemstgen_omp

makesmidb_SOURCES = makesmidb.cc cryptopp_hash.h lmdb++.h

maketfmdb_SOURCES = maketfmdb.cc chemstgen.h lmdb++.h

dumpdb_SOURCES = dumpdb.cc lmdb++.h

chemstgen_SOURCES = chemstgen.cc chemstgen.h cryptopp_hash.h lmdb++.h

chemstgen_omp_SOURCES = chemstgen.cc chemstgen.h cryptopp_hash.h lmdb++.h
chemstgen_omp_CXXFLAGS = -fopenmp
