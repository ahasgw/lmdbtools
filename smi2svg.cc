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
#include <Helium/chemist/depict.h>
#include "crypto_hash.h"

namespace {

  bool std_out = false;  // output to stdout
  bool genkey = false;  // use generated key as the file name
  bool verbose = false;  // verbose output

  void writeSVG(const std::string &name, const Helium::Chemist::Molecule &mol) {
    using namespace std;
    using namespace Helium::Chemist;

    string path = (std_out ? "/dev/stdout" : name + ".svg");
    ofstream ofs(path);
    if (ofs) {
      auto coords = generate_diagram(mol);
      // generate the depiction
      SVGPainter painter(ofs);
      Depict depict(&painter);
      depict.setOption(Depict::AromaticCircle);
      //depict.setOption(Depict::NoMargin);
      depict.setPenWidth(2);
      depict.drawMolecule(mol, relevant_cycles(mol), coords);
    }
  }

  void draw_molecule_svg(const std::string &fname) {
    using namespace std;
    using namespace chemstgen;
    using namespace Helium::Chemist;

    ifstream ifs((fname == "-") ? "/dev/stdin" : fname);
    for (string smi; ifs >> smi;) {
      Molecule mol;
      Smiles SMILES;
      if (!SMILES.read(smi, mol)) {
        if (verbose) {
          cerr << SMILES.error().what() << flush;
        }
      } else {
        string name = smi;
        if (genkey) {
          name = b64urlenc_sha256(smi);
        }
        writeSVG(name, mol);
      }
    }
  }

}  // namespace

int main(int argc, char *argv[]) {
  using namespace std;

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <smilesfile> ...\n"
    "options: -c  output to stdout\n"
    "         -k  generate hash key\n"
    "         -v  verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":ckv");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'c': { std_out = true; break; }
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
      draw_molecule_svg(argv[i]);
    }
  } else {
    draw_molecule_svg("/dev/stdin");
  }
  return EXIT_SUCCESS;
}
