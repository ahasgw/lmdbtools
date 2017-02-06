#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <gflags/gflags.h>
#include "lmdb++.h"
#include "chemstgen.h"

DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(genkey, true, "generate key");
DEFINE_bool(overwrite, false, "overwrite new value for a duplicate key");
DEFINE_uint64(mapsize, 1000, "lmdb map size in MiB");

namespace {

  int make_db(int argc, char *argv[]) {
    using namespace std;
    using namespace chemstgen;

    const unsigned long max_rid = 9999;
    const unsigned long max_tid =   99;
    const unsigned int put_flags = (FLAGS_overwrite ? 0 : MDB_NOOVERWRITE);

    try {
      auto env = lmdb::env::create();
      env.set_mapsize(FLAGS_mapsize * 1024UL * 1024UL);
      env.open(argv[1], MDB_NOSUBDIR | MDB_NOLOCK);

      auto wtxn = lmdb::txn::begin(env);
      auto dbi  = lmdb::dbi::open(wtxn);

      size_t nline = 0;
      smatch match;
      const regex spaces(R"(\s+)");
      // % reactid tfmid[ comm]
      const regex idline(R"(^%\s*(\d+)\s+(\d+)\s*$)");

      ifstream ifs((string(argv[2]) == "-") ? "/dev/stdin" : argv[2]);
      if (FLAGS_genkey) {
        // mul smr[ dict ...]
        const regex tfmline(R"(^(\d+\s+\S+(\s+\S.*?)?)\s*$)");
        string key;
        for (string line; getline(ifs, line);) {
          ++nline;
          if (line.empty() || line[0] == '#') {  // skip empty or comment line
          }
          else if (line[0] == '%' && regex_match(line, match, idline)) {
            auto rid = stoul(match[1]);
            auto tid = stoul(match[2]);

            if (rid > max_rid) {
              cerr << "error: line " << nline << ": reaction id " << rid <<
                " is greater than " << max_rid << ". ignore" << endl;
              key.clear();
            }
            else if (tid > max_tid) {
              cerr << "error: line " << nline << ": transform id " << tid <<
                " is greater than " << max_tid << ". ignore" << endl;
              key.clear();
            }

            ostringstream oss;
            oss << setfill('0') << setw(4) << rid <<
              setfill('0') << setw(2) << tid;
            key = oss.str();
          }
          else if (!key.empty() && regex_match(line, match, tfmline)) {
            string val = match[1];  // transform
            Tfm tfm;
            if (tfm.init(key, val)) {
              val = regex_replace(val, spaces, "\t");
              if (!dbi.put(wtxn, key.c_str(), val.c_str(), put_flags)) {
                if (FLAGS_verbose) {
                  check_duplicates(dbi, wtxn, key, val);
                }
              }
            }
            key.clear();
          }
        }
      } else {
        // key mul smr[ dict ...]
        const regex tfmline(R"(^(\S+)\s+(\d+\s+\S+(\s+\S.*?)?)\s*$)");
        for (string line; getline(ifs, line);) {
          ++nline;
          if (line.empty() || line[0] == '#') {  // skip empty or comment line
          }
          else if (regex_match(line, match, tfmline)) {
            const string &key = match[1];  // id
            const string &val = match[2];  // transform
            if (!dbi.put(wtxn, key.c_str(), val.c_str(), put_flags)) {
              if (FLAGS_verbose) {
                check_duplicates(dbi, wtxn, key, val);
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
      cerr << e.what() << endl;
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

}  // namespace

int main(int argc, char *argv[]) {
  std::string progname = basename(argv[0]);
  std::string usage = "usage: " + progname +
    " [options] <dbname> <datafile>";
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 3) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return make_db(argc, argv);
}
