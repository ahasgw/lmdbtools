#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <unistd.h>
#include <iostream>
#include <regex>
#include <string>
#include "lmdb++.h"

int main(int argc, char *argv[]) {
  using namespace std;

  int verbose = 0;  // verbose output
  string separator = "\t";  // field separator
  string pattern = "";  // regular expression pattern
  bool stat = false;  // dump database statistics only
  bool withkey = true;  // dump with hash key
  bool valkeyorder = false;  // dump database in value-key order

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <dbname> ...\n"
    "options: -n          dump database statistics only\n"
    "         -K          dump values only without keys\n"
    "         -r          dump database in value-key reverse order\n"
    "         -s <str>    field separator (" + separator + ")\n"
    "         -p <regex>  regular expression pattern\n"
    "         -v          verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":nKrs:p:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'n': { stat = true; break; }
        case 'K': { withkey = false; break; }
        case 'r': { valkeyorder = true; break; }
        case 's': { separator = optarg; break; }
        case 'p': { pattern = optarg; break; }
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

  cout.sync_with_stdio(false);
  try {
    for (int i = optind; i < argc; ++i) {
      if (verbose > 0) {
        cerr << argv[i] << endl;
      }
      auto env = lmdb::env::create();
      env.set_mapsize(0);
      env.open(argv[i], MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

      auto rtxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
      auto dbi  = lmdb::dbi::open(rtxn);

      if (stat) {
        auto st   = dbi.stat(rtxn);
        cout << argv[i] << separator << st.ms_entries << '\n';
      } else {
        auto cursor = lmdb::cursor::open(rtxn, dbi);
        string k, v;
        if (pattern.empty()) {
          if (withkey && valkeyorder) {
            while (cursor.get(k, v, MDB_NEXT)) {
              cout << v << separator << k << '\n';
            }
          } else if (withkey && !valkeyorder) {
            while (cursor.get(k, v, MDB_NEXT)) {
              cout << k << separator << v << '\n';
            }
          } else {
            while (cursor.get(k, v, MDB_NEXT)) {
              cout << v << '\n';
            }
          }
        } else {
          const regex pat(pattern);
          if (withkey && valkeyorder) {
            while (cursor.get(k, v, MDB_NEXT)) {
              if (regex_search(k, pat)) {
                cout << v << separator << k << '\n';
              }
            }
          } else if (withkey && !valkeyorder) {
            while (cursor.get(k, v, MDB_NEXT)) {
              if (regex_search(k, pat)) {
                cout << k << separator << v << '\n';
              }
            }
          } else {
            while (cursor.get(k, v, MDB_NEXT)) {
              if (regex_search(k, pat)) {
                cout << v << '\n';
              }
            }
          }
        }
        cout << flush;
        cursor.close();
      }
      rtxn.abort();
    }
  }
  catch (const lmdb::error &e) {
    cout << flush;
    cerr << e.what() << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
