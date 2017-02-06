#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <libgen.h>
#include <string>
#include <gflags/gflags.h>
#include "lmdb++.h"

DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(checkvaluetoo, false, "check not only the key but also its value");
DEFINE_uint64(mapsize, 1000000, "lmdb map size in MiB");

namespace {

  int merge_db(int argc, char *argv[]) {
    using namespace std;

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
        if (!FLAGS_checkvaluetoo) {
          lmdb::val key;
          while (cursor.get(key, MDB_NEXT)) {
            dbi0.del(wtxn0, key);
          }
        } else {
          lmdb::val key, val, val0;
          string s, s0;
          while (cursor.get(key, val, MDB_NEXT)) {
            if (dbi0.get(wtxn0, key, val0)) {
              const string s(val.data(), val.size());
              const string s0(val0.data(), val0.size());
              if (s == s0) {
                dbi0.del(wtxn0, key);
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
  std::string progname = basename(argv[0]);
  std::string usage = "usage: " + progname +
    " [options] <targetdb> <dbname> ...";
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 3) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return merge_db(argc, argv);
}
