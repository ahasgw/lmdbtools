#include <config.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>
#include <Helium/chemist/algorithms.h>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/rings.h>
#include <Helium/chemist/smiles.h>
#include <Helium/chemist/smirks.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <gflags/gflags.h>
#include "cryptopp_hash.h"
#include "lmdb++.h"
#include "chemstgen.h"

DEFINE_bool(verbose, false, "verbose output");
DEFINE_bool(overwrite, false, "overwrite new value for a duplicate key");
DEFINE_uint64(itfmdbmapsize,     100, "input transform lmdb mapsize in MiB");
DEFINE_uint64(ismidbmapsize,   10000, "input smiles lmdb mapsize in MiB");
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
          opendb(itfmenv_, argv[1], FLAGS_itfmdbmapsize, MDB_RDONLY);
          opendb(ismienv_, argv[2], FLAGS_ismidbmapsize, MDB_RDONLY);
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

  int chemstgen(int &argc, char *argv[]) {
    try {
      // open environments
      Db db(argc, argv);

      // for each transform
      auto itfmrtxn = lmdb::txn::begin(db.itfmenv(), nullptr, MDB_RDONLY);
      auto itfmcsr = lmdb::cursor::open(itfmrtxn, lmdb::dbi::open(itfmrtxn));
      string itfmkey, itfmval;
#pragma omp parallel
#pragma omp single nowait
      while (itfmcsr.get(itfmkey, itfmval, MDB_NEXT))
#pragma omp task firstprivate(itfmkey, itfmval), shared(db)
      {
        Tfm tfm;
        if (tfm.init(itfmkey, itfmval)) {
          chrono::time_point<chrono::system_clock> tp0, tp1;
          tp0 = chrono::system_clock::now();
#pragma omp critical
          cout << itfmkey << endl;

          // for each new smiles
          auto ismirtxn = lmdb::txn::begin(db.ismienv(), nullptr, MDB_RDONLY);
          auto ismicsr = lmdb::cursor::open(ismirtxn, lmdb::dbi::open(ismirtxn));
          string ismikey, ismival;
#pragma omp parallel
#pragma omp single nowait
          while (ismicsr.get(ismikey, ismival, MDB_NEXT))
#pragma omp task firstprivate(ismikey, ismival), shared(db)
          {
            Smi ismi;
            if (ismi.init(ismikey, ismival)) {
              apply_transform_to_smiles(tfm, ismi, db);
            }
          }
          ismirtxn.abort();
          tp1 = chrono::system_clock::now();
          chrono::duration<double> elapsed_second = tp1 - tp0;
#pragma omp critical
          cout << '\t' << itfmkey << '\t' << elapsed_second.count() << endl;
        }
      }
      itfmcsr.close();
      itfmrtxn.abort();
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
//      "<itfmdb> <inewdb> <ismidb> <irtedb> <onewdb> <osmidb> <ortedb>",
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
