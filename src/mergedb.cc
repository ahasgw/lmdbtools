#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>
#include <cppformat/format.h>
#include <gflags/gflags.h>
#include "lmdb++.h"
#include "chemstgen.h"

DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(overwrite, false, "overwrite new value for a duplicate key");
DEFINE_uint64(mapsize, 1000000, "lmdb map size in MiB");
DEFINE_string(pattern, "", "regular expression pattern");

namespace {

  int merge_db(int argc, char *argv[]) {
    using namespace std;
    using namespace chemstgen;

    const unsigned int put_flags = (FLAGS_overwrite ? 0 : MDB_NOOVERWRITE);

    try {
      auto env0 = lmdb::env::create();
      env0.set_mapsize(FLAGS_mapsize * 1024UL * 1024UL);
      env0.open(argv[1], MDB_NOSUBDIR | MDB_NOLOCK);

      auto wtxn0 = lmdb::txn::begin(env0, nullptr);
      auto dbi0  = lmdb::dbi::open(wtxn0);

      for (int i = 2; i < argc; ++i) {
        auto env = lmdb::env::create();
        env.set_mapsize(FLAGS_mapsize * 1024UL * 1024UL);
        env.open(argv[i], MDB_NOSUBDIR | MDB_NOLOCK | MDB_RDONLY);

        auto rtxn   = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
        auto dbi    = lmdb::dbi::open(rtxn);

        auto cursor = lmdb::cursor::open(rtxn, dbi);
        string key, val;
        if (FLAGS_pattern.empty()) {
          while (cursor.get(key, val, MDB_NEXT)) {
            if (!dbi0.put(wtxn0, key.c_str(), val.c_str(), put_flags)) {
              if (FLAGS_verbose) {
                check_duplicates(dbi0, wtxn0, key, val);
              }
            }
          }
        } else {
          const regex pattern(FLAGS_pattern);
          while (cursor.get(key, val, MDB_NEXT)) {
            if (regex_search(key, pattern)) {
              if (!dbi0.put(wtxn0, key.c_str(), val.c_str(), put_flags)) {
                if (FLAGS_verbose) {
                  check_duplicates(dbi0, wtxn0, key, val);
                }
              }
            }
          }
        }
        cursor.close();
        rtxn.abort();
      }
      wtxn0.commit();
    }
    catch (const lmdb::error &e) {
      cerr << e.what() << endl;
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

}  // namespace

int main(int argc, char *argv[]) {
  std::string usage = fmt::format("usage: {} <destdb> <dbname> ...", argv[0]);
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 3) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return merge_db(argc, argv);
}
