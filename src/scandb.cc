#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <gflags/gflags.h>
#include "lmdb++.h"

DEFINE_bool(key, true, "dump with hash key");
DEFINE_bool(valuekey, false, "dump database value-key order in a row");
DEFINE_string(separator, "\t", "field separator");

namespace {

  int scan_db(int argc, char *argv[]) {
    using namespace std;
    cout.sync_with_stdio(false);
    try {
      auto env = lmdb::env::create();
      env.set_mapsize(0);
      env.open(argv[1], MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

      auto rtxn   = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
      auto dbi    = lmdb::dbi::open(rtxn);

      string linefmt = (FLAGS_key
          ? (FLAGS_valuekey ? "{2}{1}{0}\n" : "{0}{1}{2}\n")
          : "{2}\n");

      for (int i = 2; i < argc; ++i) {
        string fname(argv[i]);
        ifstream ifs((fname == "-") ? "/dev/stdin" : fname);
        for (string key; ifs >> key;) {
          lmdb::val k{key.data(), key.size()};
          lmdb::val v;
          if (dbi.get(rtxn, k, v)) {
            const string value(v.data(), v.size());
            fmt::print(cout, linefmt, key, FLAGS_separator, value);
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

}  // namespace

int main(int argc, char *argv[]) {
  std::string usage = fmt::format("usage: {} <dbname> <idfile> ...", argv[0]);
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 2) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return scan_db(argc, argv);
}
