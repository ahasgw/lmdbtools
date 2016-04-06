bin_PROGRAMS = dumpdb makesmidb maketfmdb mergedb subtrdb smi2svg chemstgen

dumpdb_SOURCES = dumpdb.cc lmdb++.h

makesmidb_SOURCES = makesmidb.cc chemstgen.h cryptopp_hash.h lmdb++.h

maketfmdb_SOURCES = maketfmdb.cc chemstgen.h lmdb++.h

mergedb_SOURCES = mergedb.cc chemstgen.h lmdb++.h

subtrdb_SOURCES = subtrdb.cc lmdb++.h

smi2svg_SOURCES = smi2svg.cc cryptopp_hash.h
smi2svg_CXXFLAGS = $(eigen3_CFLAGS) $(eigen3_LIBS)

chemstgen_SOURCES = chemstgen.cc chemstgen.h cryptopp_hash.h lmdb++.h
