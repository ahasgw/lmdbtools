#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <gflags/gflags.h>
#include "lmdb++.h"

DEFINE_bool(verbose, false, "verbose output");
DEFINE_uint64(mapsize, 1000000, "lmdb map size in MiB");

namespace {

  int fwd2rev_db(int argc, char *argv[]) {
    using namespace std;
    cin.sync_with_stdio(false);

    try {
      auto env = lmdb::env::create();
      env.set_mapsize(FLAGS_mapsize * 1024UL * 1024UL);
      env.open(argv[1], MDB_NOSUBDIR | MDB_NOLOCK);

      auto wtxn = lmdb::txn::begin(env);
      auto dbi  = lmdb::dbi::open(wtxn);

      // reactanthash.tfmid producthash ...
      const regex pattern(R"(^(\S+)\.(\d+)\s(.*)$)");
      smatch match;
      for (int i = 2; i < argc; ++i) {
        ifstream ifs((string(argv[i]) == "-") ? "/dev/stdin" : argv[i]);

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
      cout << argv[1] << '\t' << st.ms_entries << endl;
      wtxn.commit();
    }
    catch (const lmdb::error &e) {
      cout << flush;
      cerr << e.what() << endl;
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

}  // namespace

int main(int argc, char *argv[]) {
  std::string progname = basename(argv[0]);
  std::string usage = "usage: " + progname +
    " [options] <dbname> <edgefile> ...";
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 2) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return fwd2rev_db(argc, argv);
}
