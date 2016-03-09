#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include <sstream>

#include <string>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/smiles.h>
#include <Helium/chemist/depict.h>
#include <gflags/gflags.h>
#include "cryptopp_hash.h"

DEFINE_bool(name, "smiles", "filename (smiles/key/genkey)";

namespace {
  
  void writeSVG(const std::string &smi, const Helium::Chemist::Molecule &mol) {
    using namespace std;
    using namespace Helium::Chemist;

    string path = smi + ".svg";
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
    using namespace Helium::Chemist;

    ifstream ifs((fname == "-") ? "/dev/stdin" : fname);
    for (string line; getline(ifs, line);) {
      string smi;
      istringstream iss(line);
      iss >> smi;
      Molecule mol;
      Smiles SMILES;
      if (!SMILES.read(smi, mol)) {
        cerr << SMILES.error().what() << flush;
      } else {
        writeSVG(smi, mol);
      }
    }
  }

}  // namespace

int main(int argc, char *argv[]) {
  std::string usage = fmt::format("usage: {} [-]|[<smilesfile> ...]", argv[0]);
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
