#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <libgen.h>
#include <unistd.h>
#include "lmdb++.h"

int main(int argc, char *argv[]) {
  using namespace std;

  int verbose = 0;  // verbose output
  string separator = "\t";  // field separator
  string pattern = R"(^(\S+).*)";
  bool withkey = true;  // dump with key
  bool valkeyorder = false;  // dump database value-key order

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <dbname> [<keyfile> ...]\n"
    "options: -p           regular expression pattern for key\n"
    "                      default pattern is \"" + pattern + "\"\n"
    "         -k           dump with key\n"
    "         -r           dump database in value-key reverse order\n"
    "         -s <string>  field separator\n"
    "         -v           verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":p:krs:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'p': { pattern = optarg; break; }
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
      cerr << "pattern: " << pattern << endl;
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

    smatch match;
    regex pat(pattern);

    for (int i = oi; i < argc; ++i) {
      if (verbose > 1) {
        cerr << "? " << argv[i] << endl;
      }
      ifstream ifs(argv[i]);
      for (string line; getline(ifs, line);) {
        if (regex_match(line, match, pat)) {
          if (match.size() <= 1)
            continue;
          const string &key = (match.size() >= 1 ? match.str(1) : "");
          const lmdb::val k{key.data(), key.size()};
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
    }
    rtxn.abort();
  }
  catch (const lmdb::error &e) {
    cout << flush;
    cerr << e.what() << endl;
    return EXIT_FAILURE;
  }
  catch (const regex_error &e) {
    cerr << e.what() << ": pattern: " << pattern << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
