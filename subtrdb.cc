#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <libgen.h>
#include <unistd.h>
#include <string>
#include "lmdb++.h"

int main(int argc, char *argv[]) {
  using namespace std;

  bool checkvaluetoo = false;  // check not only the key but also its value
  uint64_t mapsize = 1000000;  // lmdb map size in MiB

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <targetdb> <dbname> ...\n"
    "options: -x         check not only the key but also its value\n"
    "         -m <size>  lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":xm:");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'x': { checkvaluetoo = true; break; }
        case 'm': { mapsize = stoul(optarg); break; }
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
  string tdbfname(argv[oi++]);

  try {
    auto env0 = lmdb::env::create();
    env0.set_mapsize(mapsize * 1024UL * 1024UL);
    env0.open(tdbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK);

    auto wtxn0 = lmdb::txn::begin(env0, nullptr);
    auto dbi0  = lmdb::dbi::open(wtxn0);

    for (int i = oi; i < argc; ++i) {
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
    wtxn0.commit();
  }
  catch (const lmdb::error &e) {
    cerr << e.what() << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
