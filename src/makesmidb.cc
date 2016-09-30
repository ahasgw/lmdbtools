#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/smiles.h>
#include <cppformat/format.h>
#include <gflags/gflags.h>
#include "cryptopp_hash.h"
#include "lmdb++.h"
#include "chemstgen.h"

DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(genkey, true, "generate hash key");
DEFINE_bool(canonicalize, true, "canonicalize smiles");
DEFINE_bool(removecomment, true, "remove comment line beginning with '#'");
DEFINE_bool(singlecomponentonly, false, "select single component smiles only");
DEFINE_bool(overwrite, false, "overwrite new value for a duplicate key");
DEFINE_uint64(mapsize, 10000, "lmdb map size in MiB");

namespace {

  int make_db(int argc, char *argv[]) {
    using namespace std;
    using namespace chemstgen;
    using namespace Helium::Chemist;

    const unsigned int put_flags = (FLAGS_overwrite ? 0 : MDB_NOOVERWRITE);

    try {
      auto env = lmdb::env::create();
      env.set_mapsize(FLAGS_mapsize * 1024UL * 1024UL);
      env.open(argv[1], MDB_NOSUBDIR | MDB_NOLOCK);

      auto wtxn = lmdb::txn::begin(env);
      auto dbi  = lmdb::dbi::open(wtxn);

      smatch match;
      ifstream ifs((string(argv[2]) == "-") ? "/dev/stdin" : argv[2]);
      if (FLAGS_genkey) {
        Molecule mol;
        Smiles SMILES;
        // smi[ comm]
        const regex pattern(FLAGS_removecomment
            ? R"(^([^#]\S*|)(\s+(.*?)\s*)?$)"
            : R"(^(\S*)(\s+(.*?)\s*)?$)"
            );
        for (string line; getline(ifs, line);) {
          if (regex_match(line, match, pattern)) {
            string val = match[1];  // smi
            if (FLAGS_canonicalize || FLAGS_singlecomponentonly) {
              SMILES.read(val, mol);
              if (FLAGS_singlecomponentonly &&
                  (num_connected_components(mol) != 1))
                continue;
              if (FLAGS_canonicalize) {
                make_hydrogens_implicit(mol);
                reset_implicit_hydrogens(mol);
                val = SMILES.writeCanonical(mol);
              }
            }
            string key =
              cryptopp_hash<CryptoPP::SHA256,CryptoPP::Base64URLEncoder>(val);
            if (!dbi.put(wtxn, key.c_str(), val.c_str(), put_flags)) {
              if (FLAGS_verbose) {
                check_duplicates(dbi, wtxn, key, val);
              }
            }
          }
        }
      } else {
        // key smi[ comm]
        const regex pattern(R"(^(\S+)\s(\S*)(\s+(.*?)\s*)?$)");
        for (string line; getline(ifs, line);) {
          if (regex_match(line, match, pattern)) {
            const string &key = match[1];  // hash
            const string &val = match[2];  // smi
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
