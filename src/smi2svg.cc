#include <config.h>
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
#include <Helium/chemist/depict.h>
#include <gflags/gflags.h>
#include "cryptopp_hash.h"

DEFINE_bool(stdout, false, "output to stdout");
DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(genkey, false, "use generated key as the file name");

namespace {

  void writeSVG(const std::string &name, const Helium::Chemist::Molecule &mol) {
    using namespace std;
    using namespace Helium::Chemist;

    string path = (FLAGS_stdout ? "/dev/stdout" : name + ".svg");
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
        if (FLAGS_verbose) {
          cerr << SMILES.error().what() << flush;
        }
      } else {
        string name = smi;
        if (FLAGS_genkey) {
          name = cryptopp_hash<CryptoPP::SHA256,
               CryptoPP::Base64URLEncoder>(smi);
        }
        writeSVG(name, mol);
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
      draw_molecule_svg(argv[i]);
    }
  } else {
    draw_molecule_svg("/dev/stdin");
  }
  return EXIT_SUCCESS;
}
