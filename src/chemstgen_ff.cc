#include <config.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>
#include <Helium/chemist/algorithms.h>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/rings.h>
#include <Helium/chemist/smiles.h>
#include <Helium/chemist/smirks.h>
#include <cppformat/format.h>
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

  void apply_replacedict(unordered_set<string> &prodset,
      const vector<vector<string>> &dict) {
    unordered_set<string> set;
    Smiles SMILES;
    for (const auto &prodcan: prodset) {
      vector<vector<string>::size_type> idx(dict.size(), 0);
      for (;;) {
        Molecule mol;
        SMILES.read(prodcan, mol);
        Smirks auxSMIRKS;
        RingSet<Molecule> rings(mol);
        for (vector<vector<string>>::size_type i = 0; i < dict.size(); ++i) {
          if (!auxSMIRKS.init(dict[i][idx[i]])) {
            cerr << "error: " << auxSMIRKS.error().what()
              << '\"' << dict[i][idx[i]] << '\"' << endl;
            throw runtime_error(dict[i][idx[i]].c_str());
          }
          auxSMIRKS.apply(mol, rings);
          // this is not effective. mol->smiles->mol
          SMILES.read(regex_replace(SMILES.write(mol), Xx, R"(*)"), mol);
        }
        // update indeces
        ++idx[dict.size() - 1];
        for (int j = dict.size() - 1; j > 0; --j) {
          if (idx[j] >= dict[j].size()) {
            idx[j] = 0;
            ++idx[j - 1];
          }
        }
        // make unique set
        make_hydrogens_implicit(mol);
        reset_implicit_hydrogens(mol);
        string output = SMILES.writeCanonical(mol);
        set.emplace(output);
        // end condition
        if (idx[0] >= dict[0].size())
          break;
      }
    }
    prodset = set;
  }

  void apply_transform_to_smiles(Tfm &tfm, Smi &ismi, Db &db) {
    unordered_set<string> prodset;
    {
      if (tfm.smirks().requiresExplicitHydrogens())
        make_hydrogens_explicit(ismi.multi_mol());
      int maxapply = FLAGS_maxapply;
      auto products = tfm.smirks().react(ismi.multi_mol(), 1, maxapply);
      // make unique set
      Smiles SMILES;
      prodset.clear();
      for (const auto &product: products) {
        Molecule prod;
        pick_marked_component(*product, prod);
        make_hydrogens_implicit(prod);
        reset_implicit_hydrogens(prod);
        string smi = SMILES.writeCanonical(prod);
        if (!smi.empty()) {
          smi = regex_replace(smi, Xx, R"(*)");
          prodset.emplace(smi);
        }
      }
    }

    if (prodset.size() > 0) {
      if (tfm.replacedict().size() > 0) {
        apply_replacedict(prodset, tfm.replacedict());
      }
      if (FLAGS_verbose) {
        for (const auto &prodcan: prodset)
          cout << '\t' << prodcan << endl;
      }
      string prodsetstr;
      string smirec = ismi.key() + '\t' + ismi.smiles() + '\n';
#pragma omp critical
      db.osmienv() << smirec << flush;
      for (const auto &prodcan: prodset) {
        string smikey =
          cryptopp_hash<CryptoPP::SHA256,CryptoPP::Base64Encoder>(prodcan);
        smirec = smikey + '\t' + prodcan + '\n';
#pragma omp critical
        db.osmienv() << smirec << flush;
        prodsetstr += ' ' + smikey;
      }
      prodsetstr[0] = '\t';
      string rterec = ismi.key() + '.' + tfm.key() + prodsetstr + '\n';
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
              if (ismi.init(ismikey, ismival, tfm)) {
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
  std::string usage = fmt::format("usage: {} "
      "<itfm.tsv> <ismi.tsv> <osmi.tsv> <orte.tsv>",
      argv[0]);
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 5) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return chemstgen::chemstgen(argc, argv);
}
