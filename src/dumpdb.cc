#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>
#include <cppformat/format.h>
#include <gflags/gflags.h>
#include "lmdb++.h"

DEFINE_bool(stat, false, "dump database statistics only");
DEFINE_bool(key, true, "dump with hash key");
DEFINE_bool(smiles, false, "dump database in SMILES format");
DEFINE_string(separator, "\t", "field separator");
DEFINE_string(pattern, "", "regular expression pattern");

namespace {

  int stat_db(int argc, char *argv[]) {
    using namespace std;
    cout.sync_with_stdio(false);
    try {
      for (int i = 1; i < argc; ++i) {
        auto env = lmdb::env::create();
        env.set_mapsize(0);
        env.open(argv[i], MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

        auto rtxn   = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
        auto dbi    = lmdb::dbi::open(rtxn);

        if (FLAGS_stat) {
          auto st   = dbi.stat(rtxn);
          cout << argv[i] << '\t' << st.ms_entries << endl;
        } else {
          auto cursor = lmdb::cursor::open(rtxn, dbi);
          string linefmt = (FLAGS_key
              ? (FLAGS_smiles ? "{2}{1}{0}\n" : "{0}{1}{2}\n")
              : "{2}\n");
          string key, value;
          if (FLAGS_pattern.empty()) {
            while (cursor.get(key, value, MDB_NEXT)) {
              fmt::print(cout, linefmt, key, FLAGS_separator, value);
            }
          } else {
            const regex pattern(FLAGS_pattern);
            while (cursor.get(key, value, MDB_NEXT)) {
              if (regex_search(key, pattern)) {
                fmt::print(cout, linefmt, key, FLAGS_separator, value);
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

}  // namespace

int main(int argc, char *argv[]) {
  std::string usage = fmt::format("usage: {} <dbname> ...", argv[0]);
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 2) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return stat_db(argc, argv);
}
