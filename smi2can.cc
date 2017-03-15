#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <libgen.h>
#include <unistd.h>
#include <regex>
#include <sstream>
#include <string>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/smiles.h>
#include "crypto_hash.h"

namespace {

  bool genkey = false;  // generate key
  bool verbose = false;  // verbose output

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
          if (verbose) {
            cerr << SMILES.error().what() << flush;
          }
        } else {
          make_hydrogens_implicit(mol);
          reset_implicit_hydrogens(mol);
          string can = SMILES.writeCanonical(mol);
          if (genkey) {
            string key = b64urlenc_sha256(can);
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
  using namespace std;

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <smilesfile> ...\n"
    "options: -k  generate hash key\n"
    "         -v  verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":kv");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'k': { genkey = true; break; }
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
  if (argc - optind > 0) {
    for (int i = optind; i < argc; ++i) {
      canonicalize(argv[i]);
    }
  } else {
    canonicalize("/dev/stdin");
  }
  return EXIT_SUCCESS;
}
