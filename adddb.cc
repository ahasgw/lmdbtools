#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>
#include <libgen.h>
#include <unistd.h>
#include "lmdb++.h"

int main(int argc, char *argv[]) {
  using namespace std;

  uint64_t mapsize = 1024UL * 1024UL;  // lmdb map size in MiB
  int verbose = 0;  // verbose output
  string pattern = "";  // regular expression pattern
  bool overwrite = false;  // overwrite new value for a duplicate key
  bool deleteval = false;  // delete value

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <destdb> [<dbname> ...]\n"
    "options: -p <string>  regular expression pattern for key\n"
    "         -o           overwrite new value for a duplicate key\n"
    "         -D           delete value\n"
    "         -m <size>    lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    "         -v           verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":p:oDm:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'p': { pattern = optarg; break; }
        case 'o': { overwrite = true; break; }
        case 'D': { deleteval = true; break; }
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
  string odbfname(argv[oi++]);

  const unsigned int put_flags = (overwrite ? 0 : MDB_NOOVERWRITE);

  try {
    auto env0 = lmdb::env::create();
    env0.set_mapsize(mapsize * 1024UL * 1024UL);
    env0.open(odbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK);

    auto wtxn0 = lmdb::txn::begin(env0, nullptr);
    auto dbi0  = lmdb::dbi::open(wtxn0);

    if (verbose > 0) {
      cerr << odbfname << endl;
    }

    for (int i = oi; i < argc; ++i) {
      if (verbose > 0) {
        cerr << "+ " << argv[i] << endl;
      }
      auto env = lmdb::env::create();
      env.set_mapsize(mapsize * 1024UL * 1024UL);
      env.open(argv[i], MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

      auto rtxn   = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
      auto dbi    = lmdb::dbi::open(rtxn);

      auto cursor = lmdb::cursor::open(rtxn, dbi);
      lmdb::val key;
      lmdb::val val;

      if (deleteval) {
        lmdb::val empty("");
        if (pattern.empty()) {
          while (cursor.get(key, val, MDB_NEXT)) {
            dbi0.put(wtxn0, key, empty);
          }
        } else {
          const regex pat(pattern);
          while (cursor.get(key, val, MDB_NEXT)) {
            string keystr(key.data(), key.size());
            if (regex_search(keystr, pat)) {
              dbi0.put(wtxn0, key, empty);
            }
          }
        }
      } else {
        if (pattern.empty()) {
          while (cursor.get(key, val, MDB_NEXT)) {
            if (!dbi0.put(wtxn0, key, val, put_flags)) {
              if (verbose > 1) {
                const string keystr(key.data(), key.size());
                cerr << "== " << keystr << endl;
              }
            }
          }
        } else {
          const regex pat(pattern);
          while (cursor.get(key, val, MDB_NEXT)) {
            string keystr(key.data(), key.size());
            if (regex_search(keystr, pat)) {
              if (!dbi0.put(wtxn0, key, val, put_flags)) {
                if (verbose > 1) {
                  const string keystr(key.data(), key.size());
                  cerr << "== " << keystr << endl;
                }
              }
            }
          }
        }
      }
      cursor.close();
      rtxn.abort();
    }

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
