#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include "lmdb++.h"

int main(int argc, char *argv[]) {
  using namespace std;

  uint64_t mapsize = 10000;  // lmdb map size in MiB
  int verbose = 0;  // verbose output
  bool overwrite = false;  // overwrite new value for a duplicate key

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <odbname> [<itxtfile> ...]\n"
    "options: -o         overwrite new value for a duplicate key\n"
    "         -m <size>  lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    "         -v         verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":om:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'o': { overwrite = true; break; }
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
  if (argc - optind < 2) {
    cout << "too few arguments\n" << usage << flush;
    exit(EXIT_FAILURE);
  }

  int oi = optind;
  string odbfname (argv[oi++]);

  const unsigned int put_flags = (overwrite ? 0 : MDB_NOOVERWRITE);

  try {
    auto env = lmdb::env::create();
    env.set_mapsize(mapsize * 1024UL * 1024UL);
    env.open(odbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK);

    auto wtxn = lmdb::txn::begin(env);
    auto dbi  = lmdb::dbi::open(wtxn);

    if (verbose > 0) {
      cerr << odbfname << endl;
    }

    smatch match;
    const regex pattern(R"(^(\S+)\s(\S*)(\s+(.*?)\s*)?$)");

    for (int i = oi; i < argc; ++i) {
      string itxtfname(argv[i]);
      if (verbose > 0) {
        cerr << "+ " << itxtfname << endl;
      }
      ifstream ifs(itxtfname);
      for (string line; getline(ifs, line);) {
        if (regex_match(line, match, pattern)) {
          const string &keystr = match[1];
          const string &valstr = match[2];
          const lmdb::val key(keystr);
          /* no const */ lmdb::val val(valstr);
          if (!dbi.put(wtxn, key, val, put_flags)) {
            if (verbose > 1) {
              cerr << "== " << keystr << endl;
            }
          }
        }
      }
    }

    MDB_stat st = dbi.stat(wtxn);
    cout << odbfname << '\t' << st.ms_entries << endl;
    wtxn.commit();
  }
  catch (const lmdb::error &e) {
    cerr << e.what() << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
