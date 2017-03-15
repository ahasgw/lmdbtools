#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <unistd.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include "lmdb++.h"
#include "chemstgen.h"

int main(int argc, char *argv[]) {
  using namespace std;
  using namespace chemstgen;

  bool genkey = false;  // generate key
  bool overwrite = false;  // overwrite new value for a duplicate key
  uint64_t mapsize = 1000;  // lmdb map size in MiB

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <dbname> <datafile>\n"
    "options: -k         generate tfm key\n"
    "         -o         overwrite new value for a duplicate key\n"
    "         -m <size>  lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    "         -v         verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":kom:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'k': { genkey = true; break; }
        case 'o': { overwrite = true; break; }
        case 'm': { mapsize = stoul(optarg); break; }
        case 'v': { verbose = true; break; }
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
  string itfmfname(argv[oi++]);

  const unsigned long max_rid = 9999;
  const unsigned long max_tid =   99;
  const unsigned int put_flags = (overwrite ? 0 : MDB_NOOVERWRITE);

  try {
    auto env = lmdb::env::create();
    env.set_mapsize(mapsize * 1024UL * 1024UL);
    env.open(odbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK);

    auto wtxn = lmdb::txn::begin(env);
    auto dbi  = lmdb::dbi::open(wtxn);

    size_t nline = 0;
    smatch match;
    const regex spaces(R"(\s+)");
    // % reactid tfmid[ comm]
    const regex idline(R"(^%\s*(\d+)\s+(\d+)\s*$)");

    ifstream ifs(itfmfname);
    if (genkey) {
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
              if (verbose) {
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
            if (verbose) {
              check_duplicates(dbi, wtxn, key, val);
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
  return EXIT_SUCCESS;
}
