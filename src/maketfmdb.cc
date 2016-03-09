#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <cppformat/format.h>
#include <gflags/gflags.h>
#include "cryptopp_hash.h"
#include "lmdb++.h"

DEFINE_bool(genkey, true, "generate hash key");
DEFINE_uint64(mapsize, 10, "lmdb map size in MiB");

namespace {

  int make_db(int argc, char *argv[]) {
    using namespace std;
    using namespace chemstgen;

    const unsigned long max_rid = 9999;
    const unsigned long max_tid =   99;

    try {
      auto env = lmdb::env::create();
      env.set_mapsize(FLAGS_mapsize * 1024UL * 1024UL);
      env.open(argv[1], MDB_NOSUBDIR | MDB_NOLOCK);

      auto wtxn = lmdb::txn::begin(env);
      auto dbi  = lmdb::dbi::open(wtxn);

      size_t nline = 0;
      smatch match;
      regex spaces(R"(\s+)");
      // % reactid tfmid[ comm]
      regex idline(R"(^%\s*([0-9]+)\s*([0-9]+)(\s+.+)?)");

      ifstream ifs((string(argv[2]) == "-") ? "/dev/stdin" : argv[2]);
      if (FLAGS_genkey) {
        // mul smr[ dict ...]
        regex tfmline(R"(^([,0-9]+\s+\S+(\s+.+)?)\s*$)");
        string key = "n/a";
        for (string line; getline(ifs, line);) {
          ++nline;
          if (line.empty() || line[0] == '#')     // skip empty or comment line
            continue;
          if (line[0] == '%' && regex_match(line, match, idline)) {
            auto rid = stoul(match[1]);
            auto tid = stoul(match[2]);

            if (rid > max_rid) {
              cerr << fmt::format(
                  "error: line {}: reaction id {} is greater than {}. ignore",
                  nline, rid, max_rid) << endl;
              key = "n/a";
            }
            if (tid > max_tid) {
              cerr << fmt::format(
                  "error: line {}: transform id {} is greater than {}. ignore",
                  nline, tid, max_tid) << endl;
              key = "n/a";
            }

            fmt::MemoryWriter sout;
            sout << fmt::pad(rid, 4, '0') << fmt::pad(tid, 2, '0');
            key = sout.str();
          }
          if (key != "n/a" && regex_match(line, match, tfmline)) {
            string val = match[1];  // transform
            val = regex_replace(val, spaces, "\t");
            dbi.put(wtxn, key.c_str(), val.c_str());
          }
        }
      } else {
        // key mul smr[ dict ...]
        regex tfmline(R"(^(\S+)\s+([\d,]+\s+\S+(\s+.+)?)\s*$)");
        for (string line; getline(ifs, line);) {
          ++nline;
          if (line.empty() || line[0] == '#')     // skip empty or comment line
            continue;
          if (regex_match(line, match, tfmline)) {
            const string &key = match[1];  // hash
            const string &val = match[2];  // transform
            dbi.put(wtxn, key.c_str(), val.c_str());
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
  std::string usage = fmt::format("usage: {} <dbname> <datafile>", argv[0]);
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 3) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return make_db(argc, argv);
}
