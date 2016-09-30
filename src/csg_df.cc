#include <config.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <omp.h>
#include <Helium/chemist/algorithms.h>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/rings.h>
#include <Helium/chemist/smiles.h>
#include <Helium/chemist/smirks.h>
#include <cppformat/format.h>
#include <gflags/gflags.h>
#include "cryptopp_hash.h"
#include "lmdb++.h"
#include "chemstgen.h"

DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(overwrite, false, "overwrite new value for a duplicate key");
DEFINE_uint64(osmidbmapsize, 1000000, "output smiles lmdb mapsize in MiB");
DEFINE_uint64(commitchunksize,  100, "maximum size of commit chunk");
DEFINE_int32(maxapply, 1, "maximum number of applying reaction");

namespace chemstgen {

  using namespace std;
  using namespace Helium::Chemist;

  const int keep_mark = 999;
  const regex Xx(R"(Xx)");

  class Db {
    public:
      Db(int argc, char *argv[]):
        itfmenv_(lmdb::env::create()),
        ismienv_(lmdb::env::create()),
        osmienv_(lmdb::env::create()),
        orteenv_(argv[4]) {
          // open environments
          opendb(itfmenv_, argv[1], UINT64_C(0), MDB_RDONLY);
          opendb(ismienv_, argv[2], UINT64_C(0), MDB_RDONLY);
          opendb(osmienv_, argv[3], FLAGS_osmidbmapsize);
        }
      ~Db() {}
      lmdb::env &itfmenv() { return itfmenv_; };
      lmdb::env &ismienv() { return ismienv_; };
      lmdb::env &osmienv() { return osmienv_; };
      ostream &orteenv()   { return orteenv_; };
    private:
      lmdb::env itfmenv_;
      lmdb::env ismienv_;
      lmdb::env osmienv_;
      ofstream  orteenv_;
    private:
      static void opendb(lmdb::env &env, const char *name, uint64_t mapsize,
          unsigned int flags = 0) {
        env.set_mapsize(mapsize * 1024UL * 1024UL);
        env.open(name, MDB_NOSUBDIR | MDB_NOLOCK | flags);
      }
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
#if 0
cout << "prodcan.smi\t" << prodcan.second << endl;
cout << "dict.size()\t" << dict.size() << endl;
cout << "dict[0].size()\t" << dict[0].size() << endl;
for (int k = 0; k < dict.size(); ++k) cout<<' '<<dict[k].size(); cout << endl;
#endif
      vector<vector<Smirks>::size_type> idx(dict.size(), 0);
      for (;;) {
        if (!SMILES.read(prodcan.second, mol)) {
          if (FLAGS_verbose) {
            cerr << "error: cannot read smiles: " << prodcan.second << endl;
          }
          break;
        }
#if 0
cout << "from here" << endl;
for (int k = 0; k < dict.size(); ++k) cout<<' '<<idx[k]; cout << endl;
#endif
        for (vector<vector<Smirks>>::size_type i = 0; i < dict.size(); ++i) {
          auxSMIRKS.init(dict[i][idx[i]]);
          if (auxSMIRKS.requiresExplicitHydrogens()) {
            make_hydrogens_explicit(mol);
          }
          auxSMIRKS.apply(mol);
          // this is not effective. mol->smiles->mol
          SMILES.read(regex_replace(SMILES.write(mol), Xx, R"(*)"), mol);
        }
#if 0
cout << "to here!" << endl;
#endif
        // make unique set
        make_hydrogens_implicit(mol);
        reset_implicit_hydrogens(mol);
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

  void apply_tfm_to_smiles(Db &db, Tfm &tfm, Smi &ismi) {
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
        make_hydrogens_implicit(prod);
        reset_implicit_hydrogens(prod);
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
#if 0
cout << "ismi.key\t" << ismi.key() << endl;
cout << "ismi.smi\t" << ismi.smiles() << endl;
cout << "tfm.multiplier\t" << tfm.multiplier() << endl;
#endif
        apply_replacedict(prodset, tfm.repldict());
      }
      if (FLAGS_verbose) {
#pragma omp critical
        for (const auto &prodcan: prodset)
          cout << '\t' << prodcan.second << endl;
      }
      const unsigned int put_flags = (FLAGS_overwrite ? 0 : MDB_NOOVERWRITE);
      string prodsetstr;
#pragma omp critical
      {
        uint64_t cnt = 0;
        auto osmiwtxn = lmdb::txn::begin(db.osmienv());
        auto osmiwdbi = lmdb::dbi::open(osmiwtxn);
        osmiwdbi.put(osmiwtxn, ismi.key().c_str(), ismi.smiles().c_str(),
            put_flags);
        for (const auto &prodcan: prodset) {
          osmiwdbi.put(osmiwtxn, prodcan.first.c_str(),
              prodcan.second.c_str(), put_flags);

          if (++cnt > FLAGS_commitchunksize) {
            // commit and reopen transaction
            osmiwtxn.commit();
            osmiwtxn = lmdb::txn::begin(db.osmienv());
            osmiwdbi = lmdb::dbi::open(osmiwtxn);
            cnt = 0;
          }

          prodsetstr += ' ' + prodcan.first;
        }
        osmiwtxn.commit();
      }
      prodsetstr[0] = '\t';
      string rterec = ismi.key() + '.' + tfm.id() + prodsetstr + '\n';
#pragma omp critical
      db.orteenv() << rterec;
    }
  }

  void apply_tfmrules_to_smiles(Db &db, vector<Tfm> &tfmrules, Smi &ismi) {
    for (auto &tfm: tfmrules) {
#if 0
      string rterec = ismi.key() + '.' + tfm.id();
#pragma omp critical
      cout << rterec << endl;
#endif
      apply_tfm_to_smiles(db, tfm, ismi);
    }
  }

  int chemstgen(int &argc, char *argv[]) {
    cout.sync_with_stdio(false);
    cerr.sync_with_stdio(false);
    try {
      // open environments
      Db db(argc, argv);

      vector<Tfm> tfmrules;
      { // read all transforms
        auto itfmrtxn = lmdb::txn::begin(db.itfmenv(), nullptr, MDB_RDONLY);
        auto itfmcsr = lmdb::cursor::open(itfmrtxn, lmdb::dbi::open(itfmrtxn));
        for (string itfmkey, itfmval; itfmcsr.get(itfmkey, itfmval, MDB_NEXT);)
        {
          Tfm tfm;
          if (!tfm.init(itfmkey, itfmval)) {
            cerr << "error: tfm " << itfmkey << endl;
          } else {
            tfmrules.emplace_back(tfm);
          }
        }
        itfmcsr.close();
        itfmrtxn.abort();
      }

      { // for each new smiles
        size_t done = 0;
        auto ismirtxn = lmdb::txn::begin(db.ismienv(), nullptr, MDB_RDONLY);
        auto ismicsr = lmdb::cursor::open(ismirtxn, lmdb::dbi::open(ismirtxn));
        string ismikey, ismival;
        {
          StSetSignalHandler sigrec;
#pragma omp parallel
#pragma omp taskgroup
#pragma omp single nowait
          while (ismicsr.get(ismikey, ismival, MDB_NEXT))
          {
#pragma omp task firstprivate(ismikey, ismival, tfmrules), shared(db, sigrec, done)
            {
#pragma omp cancel taskgroup if (sigrec)
              Smi ismi;
              if (!ismi.init(ismikey, ismival)) {
#pragma omp critical
                cerr << "error: smiles " << ismikey << endl;
              } else {
                apply_tfmrules_to_smiles(db, tfmrules, ismi);
              }
#if 0
              int th = omp_get_thread_num();
#pragma omp critical
              cout << int(sigrec) << ' ' << th << ' ' << ismikey << endl;
#endif
              ++done;
#pragma omp cancellation point taskgroup
            }
          }
        }
        ismicsr.close();
        ismirtxn.abort();
        cout << done << " SMILES have been processed" << endl;
      }
    }
    catch (const lmdb::error &e) {
      cerr << e.what() << endl;
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

}  // namespace chemstgen

int main(int argc, char *argv[]) {
  std::string usage = fmt::format("usage: {} "
      "<itfm.db> <ismi.db> <osmi.db> <orte.tsv>",
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
