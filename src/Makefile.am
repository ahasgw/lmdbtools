bin_PROGRAMS = dumpdb makesmidb maketfmdb mergedb subtrdb smi2svg \
	       chemstgen_dd chemstgen_df chemstgen_ff csg_df

dumpdb_SOURCES = dumpdb.cc lmdb++.h

makesmidb_SOURCES = makesmidb.cc chemstgen.h cryptopp_hash.h lmdb++.h

maketfmdb_SOURCES = maketfmdb.cc chemstgen.h lmdb++.h

mergedb_SOURCES = mergedb.cc chemstgen.h lmdb++.h

subtrdb_SOURCES = subtrdb.cc lmdb++.h

smi2svg_SOURCES = smi2svg.cc cryptopp_hash.h
smi2svg_CXXFLAGS = $(eigen3_CFLAGS) $(eigen3_LIBS)

chemstgen_dd_SOURCES = chemstgen_dd.cc chemstgen.h cryptopp_hash.h lmdb++.h
chemstgen_df_SOURCES = chemstgen_df.cc chemstgen.h cryptopp_hash.h lmdb++.h
chemstgen_ff_SOURCES = chemstgen_ff.cc chemstgen.h cryptopp_hash.h
csg_df_SOURCES = csg_df.cc chemstgen.h cryptopp_hash.h lmdb++.h
