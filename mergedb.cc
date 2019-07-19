#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <unistd.h>
#include <iostream>
#include <regex>
#include <string>
#include "lmdb++.h"
#include "common.h"

int main(int argc, char *argv[]) {
  using namespace std;

  string pattern = "";  // regular expression pattern
  bool overwrite = false;  // overwrite new value for a duplicate key
  uint64_t mapsize = 1000000;  // lmdb map size in MiB
  bool verbose = false;  // verbose output

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <destdb> <dbname0> <dbname1> ...\n"
    "options: -p <string>  regular expression pattern for key\n"
    "         -o           overwrite new value for a duplicate key\n"
    "         -m <size>    lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    "         -v           verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":p:om:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'p': { pattern = optarg; break; }
        case 'o': { overwrite = true; break; }
        case 'm': { mapsize = stoul(optarg); break; }
        case 'v': { verbose = true; break; }
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
  string odbfname(argv[oi++]);

  const unsigned int put_flags = (overwrite ? 0 : MDB_NOOVERWRITE);

  try {
    auto env0 = lmdb::env::create();
    env0.set_mapsize(mapsize * 1024UL * 1024UL);
    env0.open(odbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK);

    auto wtxn0 = lmdb::txn::begin(env0, nullptr);
    auto dbi0  = lmdb::dbi::open(wtxn0);

    for (int i = oi; i < argc; ++i) {
      auto env = lmdb::env::create();
      env.set_mapsize(mapsize * 1024UL * 1024UL);
      env.open(argv[i], MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

      auto rtxn   = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
      auto dbi    = lmdb::dbi::open(rtxn);

      auto cursor = lmdb::cursor::open(rtxn, dbi);
      string key, val;
      if (pattern.empty()) {
        while (cursor.get(key, val, MDB_NEXT)) {
          if (!dbi0.put(wtxn0, key.c_str(), val.c_str(), put_flags)) {
            if (verbose) {
              check_duplicates(dbi0, wtxn0, key, val);
            }
          }
        }
      } else {
        const regex pattern(pattern);
        while (cursor.get(key, val, MDB_NEXT)) {
          if (regex_search(key, pattern)) {
            if (!dbi0.put(wtxn0, key.c_str(), val.c_str(), put_flags)) {
              if (verbose) {
                check_duplicates(dbi0, wtxn0, key, val);
              }
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
