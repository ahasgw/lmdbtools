#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <Helium/chemist/algorithms.h>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/smiles.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <gflags/gflags.h>

DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(kekulize, false, "kekulize");
DEFINE_bool(aromatize, false, "aromatize");
DEFINE_bool(makehsexplicit, false, "make all hydrogens explicit");

namespace {

  void to_gsp(const std::string &fname) {
    using namespace std;
    using namespace Helium::Chemist;

    uint64_t molidx = 0;
    uint64_t vmax = 0;
    map<int, uint64_t> elm2num;

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
          if (FLAGS_makehsexplicit) {
            make_hydrogens_explicit(mol);
          }
          if (FLAGS_kekulize && !FLAGS_aromatize) {
            kekulize(mol);
          } else if (!FLAGS_kekulize && FLAGS_aromatize) {
            aromatize(mol);
          }

          fmt::print("t # {} -1 mol\n", molidx++);
          for (const auto &atom: mol.atoms()) {
            const int idx = atom.index();
            const int elm = atom.element();
            if (elm2num.find(elm) == elm2num.end()) {
              elm2num[elm] = ++vmax;
            }
            fmt::print("v {} {}\n", idx + 1, elm2num[elm]);
          }
          for (const auto &bond: mol.bonds()) {
            const int sid = bond.source().index();
            const int tid = bond.target().index();
            fmt::print("e {} {} {}\n", sid + 1, tid + 1, bond.order());
          }
          fmt::print("\n");
        }
      }
    }
  }

}  // namespace

int main(int argc, char *argv[]) {
  std::string usage = fmt::format("usage: {} <smilesfile> ...", argv[0]);
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      to_gsp(argv[i]);
    }
  } else {
    to_gsp("/dev/stdin");
  }
  return EXIT_SUCCESS;
}
