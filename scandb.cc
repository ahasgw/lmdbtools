#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <string>
#include "lmdb++.h"

int main(int argc, char *argv[]) {
  using namespace std;

  int verbose = 0;  // verbose output
  string separator = "\t";  // field separator
  bool withkey = true;  // dump with key
  bool valkeyorder = false;  // dump database value-key order

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <dbname> [<keyfile> ...]\n"
    "options: -k           dump with key\n"
    "         -r           dump database in value-key reverse order\n"
    "         -s <string>  field separator\n"
    "         -v           verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":krs:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'k': { withkey = true; break; }
        case 'r': { valkeyorder = true; break; }
        case 's': { separator = optarg; break; }
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
  string idbfname(argv[oi++]);

  cout.sync_with_stdio(false);

  try {
    if (verbose > 0) {
      cerr << idbfname << endl;
    }
    auto env = lmdb::env::create();
    env.set_mapsize(0);
    env.open(idbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

    auto rtxn   = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
    auto dbi    = lmdb::dbi::open(rtxn);

    string linefmt = (withkey
        ? (valkeyorder ? "{2}{1}{0}\n" : "{0}{1}{2}\n")
        : "{2}\n");

    for (int i = oi; i < argc; ++i) {
      if (verbose > 1) {
        cerr << "? " << argv[i] << endl;
      }
      ifstream ifs(argv[i]);
      for (string key; ifs >> key;) {
        lmdb::val k{key.data(), key.size()};
        lmdb::val v;
        if (dbi.get(rtxn, k, v)) {
          const string value(v.data(), v.size());
          if (withkey && valkeyorder) {
            cout << value << separator << key << '\n';
          } else if (withkey && !valkeyorder) {
            cout << key << separator << value << '\n';
          } else {
            cout << value << '\n';
          }
        }
      }
    }
    rtxn.abort();
  }
  catch (const lmdb::error &e) {
    cout << flush;
    cerr << e.what() << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
