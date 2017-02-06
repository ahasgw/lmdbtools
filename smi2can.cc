#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <libgen.h>
#include <regex>
#include <sstream>
#include <string>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/smiles.h>
#include <gflags/gflags.h>
#include "cryptopp_hash.h"

DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(genkey, false, "generate key");

namespace {

  void canonicalize(const std::string &fname) {
    using namespace std;
    using namespace chemstgen;
    using namespace Helium::Chemist;

    const regex pattern(R"(^(\S*)(\s+(.*?)\s*)?$)");
    smatch match;
    ifstream ifs((fname == "-") ? "/dev/stdin" : fname);
    for (string line; getline(ifs, line);) {
      if (regex_match(line, match, pattern)) {
        string smi = match[1];
        string com = match[3];

        Molecule mol;
        Smiles SMILES;
        if (!SMILES.read(smi, mol)) {
          if (FLAGS_verbose) {
            cerr << SMILES.error().what() << flush;
          }
        } else {
          make_hydrogens_implicit(mol);
          reset_implicit_hydrogens(mol);
          string can = SMILES.writeCanonical(mol);
          if (FLAGS_genkey) {
            string key = cryptopp_hash<CryptoPP::SHA256,
                   CryptoPP::Base64URLEncoder>(can);
            cout << key << "\t" << can << "\n";
          } else {
            cout << can;
            if (!com.empty()) {
              cout << "\t" << com;
            }
            cout << "\n";
          }
        }
      }
    }
  }

}  // namespace

int main(int argc, char *argv[]) {
  std::string progname = basename(argv[0]);
  std::string usage = "usage: " + progname +
    " [options] <smilesfile> ...";
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      canonicalize(argv[i]);
    }
  } else {
    canonicalize("/dev/stdin");
  }
  return EXIT_SUCCESS;
}
