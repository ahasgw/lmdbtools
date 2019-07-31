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

  uint64_t mapsize = 1024UL * 1024UL;  // lmdb map size in MiB
  uint64_t chunksize = 0;  // commit chunk size; 0: disable
  int verbose = 0;  // verbose output
  string pattern = R"(^(\S+)\s(\S*).*)";  //R"(^(\S+)\s(\S*)(\s+(.*?)\s*)?$)"
  bool overwrite = false;  // overwrite new value for a duplicate key
  bool deleteval = false;  // delete value

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <odbname> [<itxtfile> ...]\n"
    "options: -p <string>  regular expression pattern for key and value\n"
    "                      default pattern is \"" + pattern + "\"\n"
    "         -o           overwrite new value for a duplicate key\n"
    "         -D           delete value\n"
    "         -m <size>    lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    "         -n <size>    commit chunk size (default 0:disable)\n"
    "         -v           verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":p:oDm:n:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'p': { pattern = optarg; break; }
        case 'o': { overwrite = true; break; }
        case 'D': { deleteval = true; break; }
        case 'm': { mapsize = stoul(optarg); break; }
        case 'n': { chunksize = stoul(optarg); break; }
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

    if (verbose > 0) {
      cerr << "pattern: " << pattern << endl;
      cerr << odbfname << endl;
    }

    uint64_t cnt = 0;
    smatch match;
    regex pat(pattern);

    auto wtxn = lmdb::txn::begin(env);
    auto dbi  = lmdb::dbi::open(wtxn);

    for (int i = oi; i < argc; ++i) {
      string itxtfname(argv[i]);
      if (verbose > 0) {
        cerr << "+ " << itxtfname << endl;
      }
      ifstream ifs(itxtfname);
      for (string line; getline(ifs, line);) {
        if (regex_match(line, match, pat)) {
          if (match.size() <= 1)
            continue;
          const string &keystr = (match.size() >= 2 ? match.str(1) : "");
          const string &valstr = (match.size() >= 3 && !deleteval
              ? match.str(2) : "");
          const lmdb::val key(keystr);
          /* no const */ lmdb::val val(valstr);
          if (!dbi.put(wtxn, key, val, put_flags)) {
            if (verbose > 1) {
              cerr << "== " << keystr << endl;
            }
          }
          else {
            if (chunksize > 0 && ++cnt >= chunksize) {
              // commit and reopen transaction
              if (verbose > 2) {
                cerr << "commit " << cnt << endl;
              }
              wtxn.commit();
              wtxn = lmdb::txn::begin(env);
              dbi  = lmdb::dbi::open(wtxn);
              cnt = 0;
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
  catch (const regex_error &e) {
    cerr << e.what() << ": pattern: " << pattern << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
