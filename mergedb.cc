#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>
#include <libgen.h>
#include <unistd.h>
#include "lmdb++.h"

int main(int argc, char *argv[]) {
  using namespace std;

  uint64_t mapsize = 1024UL * 1024UL;  // lmdb map size in MiB
  int verbose = 0;  // verbose output
  string delimiter = ",";  // delimiter of values
  bool overwrite = false;  // overwrite new value for a duplicate key
  bool deleteval = false;  // delete value

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <odbname> <dbname1> <dbname2>\n"
    "options: -d <string>  delimiter of values\n"
    "         -m <size>    lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    "         -v           verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":d:m:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'd': { delimiter = optarg; break; }
        case 'm': { mapsize = stoul(optarg); break; }
        case 'v': { ++verbose; break; }
        case ':': { cout << "missing argument of -"
                    << static_cast<char>(optopt) << endl;
                    exit(EXIT_FAILURE);
                  }
        case '?':
        default:  { cout << "unknown option -"
                    << static_cast<char>(optopt) << '\n' << usage << flush;
                    exit(EXIT_FAILURE);
                  }
      }
    }
    catch (...) {
      cout << "invalid argument: " << argv[optind - 1] << endl;
      exit(EXIT_FAILURE);
    }
  }
  if (argc - optind < 3) {
    cout << "too few arguments\n" << usage << flush;
    exit(EXIT_FAILURE);
  }

  int oi = optind;
  const string odbfname(argv[oi++]);
  const string idbfname1(argv[oi++]);
  const string idbfname2(argv[oi++]);

  const unsigned int put_flags = (overwrite ? 0 : MDB_NOOVERWRITE);

  try {
    auto env0 = lmdb::env::create();
    env0.set_mapsize(mapsize * 1024UL * 1024UL);
    env0.open(odbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK);

    auto wtxn0 = lmdb::txn::begin(env0, nullptr);
    auto dbi0  = lmdb::dbi::open(wtxn0);


    auto env1 = lmdb::env::create();
    env1.set_mapsize(mapsize * 1024UL * 1024UL);
    env1.open(idbfname1.c_str(), MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

    auto rtxn1 = lmdb::txn::begin(env1, nullptr, MDB_RDONLY);
    auto dbi1  = lmdb::dbi::open(rtxn1);


    auto env2 = lmdb::env::create();
    env2.set_mapsize(mapsize * 1024UL * 1024UL);
    env2.open(idbfname2.c_str(), MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

    auto rtxn2 = lmdb::txn::begin(env2, nullptr, MDB_RDONLY);
    auto dbi2  = lmdb::dbi::open(rtxn2);


    MDB_stat st0 = dbi0.stat(wtxn0);
    MDB_stat st1 = dbi1.stat(rtxn1);
    MDB_stat st2 = dbi2.stat(rtxn2);
    cout << odbfname << "(" << st0.ms_entries << ") <-- "
      << idbfname1 << "(" << st1.ms_entries << ") U "
      << idbfname2 << "(" << st2.ms_entries << ")" << endl;

#if 0
    const MDB_cmp_func *cmpf1 = rtxn1.handle()->mt_dbxs[dbi1.handle()].md_cmp;
    const MDB_cmp_func *cmpf2 = rtxn2.handle()->mt_dbxs[dbi2.handle()].md_cmp;
    if (cmpf1 != cmpf2) {
      cerr << "error: comparator miss match" << endl;
      exit(EXIT_FAILURE);
    }
#endif

    auto cursor1 = lmdb::cursor::open(rtxn1, dbi1);
    lmdb::val key1;
    lmdb::val val1;

    auto cursor2 = lmdb::cursor::open(rtxn2, dbi2);
    lmdb::val key2;
    lmdb::val val2;

    size_t ncollision = 0;
    int cmp = 0;
    bool read1 = true;
    bool read2 = true;
    while (read1 && read2) {
      if (cmp <= 0) {  // read next item from db1
        read1 = cursor1.get(key1, val1, MDB_NEXT);
      }
      if (cmp >= 0) {  // read next item from db2
        read2 = cursor2.get(key2, val2, MDB_NEXT);
      }

#if 0
      const string key1str(key1.data(), key1.size());
      const string key2str(key2.data(), key2.size());
      if (read1) cout << "1 " << key1str << endl;
      if (read2) cout << "2 " << key2str << endl;
      cout << "cmp(" <<{aka ‘struct MDB_txn’} key1str << ", " << key2str << ") = " << cmp << endl;
#endif

      if (read1 && read2) {  // if both items are ready
        cmp = mdb_cmp(rtxn1, dbi1, key1, key2);

        if (cmp < 0) {
          dbi0.put(wtxn0, key1, val1);
          //cout << "write 1 " << key1str << endl;
        }
        else if (cmp > 0) {
          dbi0.put(wtxn0, key2, val2);
          //cout << "write 2 " << key2str << endl;
        }
        else /* if (cmp == 0) */ {
          string newvalstr(val1.data(), val1.size());
          newvalstr += delimiter;
          newvalstr += string(val2.data(), val2.size());
          lmdb::val newval(newvalstr);

          dbi0.put(wtxn0, key1, newval);
          //cout << "write 12 " << key1str << endl;
          if (verbose > 2) {
            const string keystr(key1.data(), key1.size());
            cerr << "== " << keystr << endl;
          }
          ++ncollision;
        }
      }
      else if (read1 && !read2) {
        // if no more item exists in db2, then
        // put all remained items in db1 to db0 and exit
        dbi0.put(wtxn0, key1, val1);
        //cout << "write 1 " << key1str << endl;
        while (cursor1.get(key1, val1, MDB_NEXT)) {
          dbi0.put(wtxn0, key1, val1);
          //cout << "write 1 " << key1str << endl;
        }
      }
      else if (!read1 && read2) {
        // if no more item exists in db1, then
        // put all remained items in db2 to db0 and exit
        dbi0.put(wtxn0, key2, val2);
        //cout << "write 2 " << key2str << endl;
        while (cursor2.get(key2, val2, MDB_NEXT)) {
          dbi0.put(wtxn0, key2, val2);
          //cout << "write 2 " << key2str << endl;
        }
      }
    }
    cout << "collision\t" << ncollision << endl;

    cursor2.close();
    cursor1.close();

    rtxn2.abort();
    rtxn1.abort();

    MDB_stat st = dbi0.stat(wtxn0);
    cout << odbfname << '\t' << st.ms_entries << endl;
    wtxn0.commit();
  }
  catch (const lmdb::error &e) {
    cerr << e.what() << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
