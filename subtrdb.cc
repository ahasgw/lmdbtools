#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>
#include <libgen.h>
#include <unistd.h>
#include "lmdb++.h"

int main(int argc, char *argv[]) {
  using namespace std;

  uint64_t mapsize = 1000000;  // lmdb map size in MiB
  int verbose = 0;  // verbose output
  bool checkvaluetoo = false;  // check not only the key but also its value

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <targetdb> [<dbname> ...]\n"
    "options: -x         check not only the key but also its value\n"
    "         -m <size>  lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    "         -v         verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":xm:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'x': { checkvaluetoo = true; break; }
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
  if (argc - optind < 1) {
    cout << "too few arguments\n" << usage << flush;
    exit(EXIT_FAILURE);
  }

  int oi = optind;
  string tdbfname(argv[oi++]);

  try {
    auto env0 = lmdb::env::create();
    env0.set_mapsize(mapsize * 1024UL * 1024UL);
    env0.open(tdbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK);

    auto wtxn0 = lmdb::txn::begin(env0, nullptr);
    auto dbi0  = lmdb::dbi::open(wtxn0);

    if (verbose > 0) {
      cerr << tdbfname << endl;
    }

    for (int i = oi; i < argc; ++i) {
      if (verbose > 0) {
        cerr << "- " << argv[i] << endl;
      }
      auto env = lmdb::env::create();
      env.set_mapsize(mapsize * 1024UL * 1024UL);
      env.open(argv[i], MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

      auto rtxn   = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
      auto dbi    = lmdb::dbi::open(rtxn);

      auto cursor = lmdb::cursor::open(rtxn, dbi);
      if (!checkvaluetoo) {
        lmdb::val key;
        while (cursor.get(key, MDB_NEXT)) {
          dbi0.del(wtxn0, key);
        }
      } else {
        lmdb::val key, val, val0;
        string s, s0;
        while (cursor.get(key, val, MDB_NEXT)) {
          if (dbi0.get(wtxn0, key, val0)) {
            const string s(val.data(), val.size());
            const string s0(val0.data(), val0.size());
            if (s == s0) {
              dbi0.del(wtxn0, key);
            }
          }
        }
      }
      cursor.close();
      rtxn.abort();
    }

    MDB_stat st = dbi0.stat(wtxn0);
    cout << tdbfname << '\t' << st.ms_entries << endl;
    wtxn0.commit();
  }
  catch (const lmdb::error &e) {
    cerr << e.what() << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
