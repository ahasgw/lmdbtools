#include <config.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <libgen.h>
#include <exception>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>
#include <Helium/chemist/algorithms.h>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/rings.h>
#include <Helium/chemist/smiles.h>
#include <Helium/chemist/smirks.h>
#include <gflags/gflags.h>
#include "cryptopp_hash.h"
#include "chemstgen.h"

DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(overwrite, false, "overwrite new value for a duplicate key");
DEFINE_int32(maxapply, 1, "maximum number of applying reaction");

namespace chemstgen {

  using namespace std;
  using namespace Helium::Chemist;

  const int keep_mark = 999;
  const regex Xx(R"(Xx)");

  class Db {
    public:
      Db(int argc, char *argv[]):
        itfmenv_(argv[1]),
        isminam_(argv[2]),
        osmienv_(argv[3]),
        orteenv_(argv[4]) {}
      ~Db() {}
      istream &itfmenv() { return itfmenv_; };
      const string &isminam() const { return isminam_; };
      ostream &osmienv() { return osmienv_; };
      ostream &orteenv() { return orteenv_; };
    private:
      ifstream itfmenv_;
      string isminam_;
      ofstream osmienv_;
      ofstream orteenv_;
  };

  void pick_marked_component(const Molecule &mol, Molecule &submol) {
    auto  num_compos =  num_connected_components(mol);
    auto atom_compos = connected_atom_components(mol);
    auto bond_compos = connected_bond_components(mol);
    vector<bool> atoms(num_atoms(mol));
    vector<bool> bonds(num_bonds(mol));
    // find remaining component that is not marked as mass = 999
    vector<bool> keep(num_compos, false);
    for (size_t i = 0; i < atom_compos.size(); ++i) {
      auto cmp = atom_compos[i];
      if (mol.atom(i).mass() == keep_mark) {
        keep[cmp] = true;
        mol.atom(i).setMass(Element::averageMass(mol.atom(i).element()));
      }
    }
    // select remaining atom components from mol
    for (size_t i = 0; i < atom_compos.size(); ++i)
      atoms[i] = keep[atom_compos[i]];
    // select remaining bond components from mol
    for (size_t i = 0; i < bond_compos.size(); ++i)
      bonds[i] = keep[bond_compos[i]];
    make_substructure(submol, mol, atoms, bonds);
  }

  void apply_replacedict(unordered_map<string, string> &prodset,
      const vector<vector<string>> &dict) {
    unordered_map<string, string> set;
    Smiles SMILES;
    Smirks auxSMIRKS;
    Molecule mol;
    for (const auto &prodcan: prodset) {
      vector<vector<string>::size_type> idx(dict.size(), 0);
      for (;;) {
        if (!SMILES.read(prodcan.second, mol)) {
          if (FLAGS_verbose) {
#pragma omp critical
            cerr << "error: cannot read smiles: " << prodcan.second << endl;
          }
          break;
        }
        for (vector<vector<string>>::size_type i = 0; i < dict.size(); ++i) {
          auxSMIRKS.init(dict[i][idx[i]]);
          if (auxSMIRKS.requiresExplicitHydrogens()) {
            make_hydrogens_explicit(mol);
          }
          auxSMIRKS.apply(mol);
          // this is not effective. mol->smiles->mol
          SMILES.read(regex_replace(SMILES.write(mol), Xx, R"(*)"), mol);
        }
        // make unique set
        reset_implicit_hydrogens(mol);
        make_hydrogens_implicit(mol);
        string output = SMILES.writeCanonical(mol);
        string outputkey =
          cryptopp_hash<CryptoPP::SHA256,CryptoPP::Base64URLEncoder>(output);
        set.emplace(outputkey, output);
        // update indeces
        ++idx[dict.size() - 1];
        for (int j = dict.size() - 1; j > 0; --j) {
          if (idx[j] >= dict[j].size()) {
            idx[j] = 0;
            ++idx[j - 1];
          }
        }
        // end condition
        if (idx[0] >= dict[0].size())
          break;
      }
    }
    prodset = set;
  }

  void apply_transform_to_smiles(Tfm &tfm, Smi &ismi, Db &db) {
    unordered_map<string, string> prodset;
    {
      int maxapply = FLAGS_maxapply;
      Smirks main_smirks;
      main_smirks.init(tfm.main_smirks_str());
      Molecule mol = ismi.multi_mol(tfm.multiplier());
      if (main_smirks.requiresExplicitHydrogens()) {
        make_hydrogens_explicit(mol);
      }
      auto products = main_smirks.react(mol, 1, maxapply);
      // make unique set
      Smiles SMILES;
      prodset.clear();
      for (const auto &product: products) {
        Molecule prod;
        pick_marked_component(*product, prod);
        reset_implicit_hydrogens(prod);
        make_hydrogens_implicit(prod);
        string smi = SMILES.writeCanonical(prod);
        if (!smi.empty()) {
          smi = regex_replace(smi, Xx, R"(*)");
          string smikey =
            cryptopp_hash<CryptoPP::SHA256,CryptoPP::Base64URLEncoder>(smi);
          prodset.emplace(smikey, smi);
        }
      }
    }

    if (prodset.size() > 0) {
      if (tfm.repldict().size() > 0) {
        apply_replacedict(prodset, tfm.repldict());
      }
      if (FLAGS_verbose) {
        for (const auto &prodcan: prodset)
          cout << '\t' << prodcan.second << endl;
      }
      string prodsetstr;
      string smirec = ismi.key() + '\t' + ismi.smiles() + '\n';
#pragma omp critical
      db.osmienv() << smirec << flush;
      for (const auto &prodcan: prodset) {
        smirec = prodcan.first + '\t' + prodcan.second + '\n';
#pragma omp critical
        db.osmienv() << smirec << flush;
        prodsetstr += ' ' + prodcan.first;
      }
      prodsetstr[0] = '\t';
      string rterec = ismi.key() + '.' + tfm.id() + prodsetstr + '\n';
#pragma omp critical
      db.orteenv() << rterec << flush;
    }
  }

  int chemstgen(int &argc, char *argv[]) {
    // open environments
    Db db(argc, argv);

    const regex tfmline(R"(^(\S+)\s+(\d+\s+\S+(\s+\S.*?)?)\s*$)");
    const regex smiline(R"(^(\S+)\s(\S*)(\+(.*?)\s*)?$)");

    // for each transform
#pragma omp parallel
#pragma omp single nowait
    for (string line; getline(db.itfmenv(), line);)
#pragma omp task firstprivate(line), shared(db)
    {
      smatch match;
      if (regex_match(line, match, tfmline)) {
        const string &itfmkey = match[1];
        const string &itfmval = match[2];
        Tfm tfm;
        if (tfm.init(itfmkey, itfmval)) {
          chrono::time_point<chrono::system_clock> tp0, tp1;
          tp0 = chrono::system_clock::now();
#pragma omp critical
          cout << itfmkey << endl;

          ifstream ifismi(db.isminam());
          // for each new smiles
#pragma omp parallel
#pragma omp single nowait
          for (string line; getline(ifismi, line);)
#pragma omp task firstprivate(tfm, line), shared(db)
          {
            smatch match;
            if (regex_match(line, match, smiline)) {
              const string &ismikey = match[1];
              const string &ismival = match[2];
              Smi ismi;
              if (ismi.init(ismikey, ismival)) {
                apply_transform_to_smiles(tfm, ismi, db);
              }
            }
          }
          tp1 = chrono::system_clock::now();
          chrono::duration<double> elapsed_second = tp1 - tp0;
#pragma omp critical
          cout << '\t' << itfmkey << '\t' << elapsed_second.count() << endl;
        }
      }
#if 1
      else {
        cout << "no match: [[" << line << "]]" << endl;
      }
#endif
    }
    return EXIT_SUCCESS;
  }

}  // namespace chemstgen

int main(int argc, char *argv[]) {
  std::string progname = basename(argv[0]);
  std::string usage = "usage: " + progname +
    " [options] <itfm.tsv> <ismi.tsv> <osmi.tsv> <orte.tsv>";
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 5) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return chemstgen::chemstgen(argc, argv);
}
