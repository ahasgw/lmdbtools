#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <iostream>
#include <regex>
#include <string>
#include <gflags/gflags.h>
#include "lmdb++.h"

DEFINE_bool(stat, false, "dump database statistics only");
DEFINE_bool(key, true, "dump with hash key");
DEFINE_bool(valuekey, false, "dump database value-key order in a row");
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
          cout << argv[i] << FLAGS_separator << st.ms_entries << '\n';
        } else {
          auto cursor = lmdb::cursor::open(rtxn, dbi);
          string key, value;
          if (FLAGS_pattern.empty()) {
            if (FLAGS_key && FLAGS_valuekey) {
              while (cursor.get(key, value, MDB_NEXT)) {
                cout << value << FLAGS_separator << key << '\n';
              }
            } else if (FLAGS_key && !FLAGS_valuekey) {
              while (cursor.get(key, value, MDB_NEXT)) {
                cout << key << FLAGS_separator << value << '\n';
              }
            } else {
              while (cursor.get(key, value, MDB_NEXT)) {
                cout << value << '\n';
              }
            }
          } else {
            const regex pattern(FLAGS_pattern);
            if (FLAGS_key && FLAGS_valuekey) {
              while (cursor.get(key, value, MDB_NEXT)) {
                if (regex_search(key, pattern)) {
                  cout << value << FLAGS_separator << key << '\n';
                }
              }
            } else if (FLAGS_key && !FLAGS_valuekey) {
              while (cursor.get(key, value, MDB_NEXT)) {
                if (regex_search(key, pattern)) {
                  cout << key << FLAGS_separator << value << '\n';
                }
              }
            } else {
              while (cursor.get(key, value, MDB_NEXT)) {
                if (regex_search(key, pattern)) {
                  cout << value << '\n';
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

}  // namespace

int main(int argc, char *argv[]) {
  std::string progname = basename(argv[0]);
  std::string usage = "usage: " + progname +
    " [options] <dbname> ...";
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 2) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return stat_db(argc, argv);
}
