#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include "lmdb++.h"

int main(int argc, char *argv[]) {
  using namespace std;

  uint64_t mapsize = 1000000;  // lmdb map size in MiB

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <dbname> <edgefile> ...\n"
    "options: -m <size>  lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":m");
    if (opt == -1) break;
    try {
      switch (opt) {
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
      cout << "invalid arguments: " << argv[optind - 1] << endl;
      exit(EXIT_FAILURE);
    }
  }
  if (argc - optind < 2) {
    cout << "too few arguments\n" << usage << flush;
    exit(EXIT_FAILURE);
  }

  int oi = optind;
  string odbfname(argv[oi++]);

  try {
    auto env = lmdb::env::create();
    env.set_mapsize(mapsize * 1024UL * 1024UL);
    env.open(odbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK);

    auto wtxn = lmdb::txn::begin(env);
    auto dbi  = lmdb::dbi::open(wtxn);

    // reactanthash.tfmid producthash ...
    const regex pattern(R"(^(\S+)\.(\d+)\s(.*)$)");
    smatch match;
    for (int i = oi; i < argc; ++i) {
      ifstream ifs(argv[i]);

      for (string line; getline(ifs, line);) {
        if (regex_match(line, match, pattern)) {
          const string &reacthash  = match[1];  // reactanthash
          const string &tfmid      = match[2];  // tfmid
          const string &prodhashes = match[3];  // producthash list
          const string val = reacthash + '.' + tfmid;

          istringstream iss(prodhashes);
          for (string key; iss >> key; ) {
            lmdb::val k{key.data(), key.size()};
            lmdb::val oldv;
            if (dbi.get(wtxn, k, oldv)) { // found
              string value(oldv.data(), oldv.size());
              const string::size_type n = value.find(val);
              if (n == string::npos) { // not already registered
                value += ' ' + val;
                dbi.put(wtxn, key.c_str(), value.c_str()); // OVERWRITE
              }
            }
            else { // not found
              dbi.put(wtxn, key.c_str(), val.c_str()); // OVERWRITE
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
    cout << flush;
    cerr << e.what() << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
