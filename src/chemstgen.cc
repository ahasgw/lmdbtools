#include <config.h>
#include <cerrno>
#include <cstdlib>
#include <iostream>
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
#include "lmdb++.h"
#include "chemstgen.h"

DEFINE_bool(verbose, false, "verbose output");
//DEFINE_uint64(dbmapsize,         10, "default lmdb mapsize in MiB");
DEFINE_uint64(itfmdbmapsize,     10, "transform lmdb mapsize in MiB");
DEFINE_uint64(inewdbmapsize,  10000, "input new smiles lmdb mapsize in MiB");
DEFINE_uint64(ismidbmapsize, 200000, "input having smiles lmdb mapsize in MiB");
DEFINE_uint64(irtedbmapsize,   1000, "input route lmdb mapsize in MiB");
DEFINE_uint64(onewdbmapsize, 200000, "output new smiles lmdb mapsize in MiB");
DEFINE_uint64(osmidbmapsize, 800000, "output smiles lmdb mapsize in MiB");
DEFINE_uint64(ortedbmapsize,  10000, "output route lmdb mapsize in MiB");
DEFINE_int32(maxapply, 1, "maximum number of applying reaction");

namespace chemstgen {

  using namespace std;
  using namespace Helium::Chemist;

  const int keep_mark = 999;
  const regex Xx(R"((Xx))");

  class Db {
    public:
      Db(int argc, char *argv[]):
        itfmenv_(lmdb::env::create()),
        inewenv_(lmdb::env::create()),
        ismienv_(lmdb::env::create()),
        irteenv_(lmdb::env::create()),
        onewenv_(lmdb::env::create()),
        osmienv_(lmdb::env::create()),
        orteenv_(lmdb::env::create()) {
          // open environments
          opendb(itfmenv_, argv[1], FLAGS_itfmdbmapsize, MDB_RDONLY);
          opendb(inewenv_, argv[2], FLAGS_inewdbmapsize, MDB_RDONLY);
          opendb(ismienv_, argv[3], FLAGS_ismidbmapsize, MDB_RDONLY);
          opendb(irteenv_, argv[4], FLAGS_irtedbmapsize, MDB_RDONLY);
          lmdb::env_copy(ismienv_, argv[6]); ismienv_.close();
          lmdb::env_copy(irteenv_, argv[7]); irteenv_.close();
          opendb(onewenv_, argv[5], FLAGS_onewdbmapsize);
          opendb(osmienv_, argv[6], FLAGS_osmidbmapsize);
          opendb(orteenv_, argv[7], FLAGS_ortedbmapsize);
        }
      ~Db() {}
      lmdb::env &itfmenv() { return itfmenv_; };
      lmdb::env &inewenv() { return inewenv_; };
      lmdb::env &ismienv() { return ismienv_; };
      lmdb::env &irteenv() { return irteenv_; };
      lmdb::env &onewenv() { return onewenv_; };
      lmdb::env &osmienv() { return osmienv_; };
      lmdb::env &orteenv() { return orteenv_; };
    private:
      lmdb::env itfmenv_;
      lmdb::env inewenv_;
      lmdb::env ismienv_;
      lmdb::env irteenv_;
      lmdb::env onewenv_;
      lmdb::env osmienv_;
      lmdb::env orteenv_;
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
            cerr << "error: " << auxSMIRKS.error().what() << endl;
            continue;
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
        if (set.count(output) == 0)
          set.emplace(output);
        // end condition
        if (idx[0] >= dict[0].size())
          break;
      }
    }
    prodset = set;
  }

  void apply_transform_to_smiles(Tfm &tfm, Smi &inew, Db &db) {
    unordered_set<string> prodset;
    {
      if (tfm.smirks().requiresExplicitHydrogens())
        make_hydrogens_explicit(inew.mol());
      int maxapply = FLAGS_maxapply;
      auto products = tfm.smirks().react(inew.mol(), 1, maxapply);
      // make unique set
      Smiles SMILES;
      prodset.clear();
      for (const auto &product: products) {
        Molecule prod;
        pick_marked_component(*product, prod);
        make_hydrogens_implicit(prod);
        reset_implicit_hydrogens(prod);
        prodset.emplace(regex_replace(SMILES.writeCanonical(prod), Xx, R"(*)"));
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
#pragma omp critical
      {
        auto onewwtxn = lmdb::txn::begin(db.onewenv());
        auto onewwdbi = lmdb::dbi::open(onewwtxn);
        auto osmiwtxn = lmdb::txn::begin(db.osmienv());
        auto osmiwdbi = lmdb::dbi::open(osmiwtxn);
        osmiwdbi.put(osmiwtxn, inew.key().c_str(), inew.smiles().c_str(),
            MDB_NOOVERWRITE);
        for (const auto &prodcan: prodset) {
          string smikey =
            cryptopp_hash<CryptoPP::SHA256,CryptoPP::Base64Encoder>(prodcan);
          if (!osmiwdbi.get(osmiwtxn, smikey.c_str())) {
            onewwdbi.put(onewwtxn, smikey.c_str(), prodcan.c_str(),
                MDB_NOOVERWRITE);
          }
          osmiwdbi.put(osmiwtxn, smikey.c_str(), prodcan.c_str(),
              MDB_NOOVERWRITE);

          prodsetstr += ' ' + smikey;
        }
        osmiwtxn.commit();
        onewwtxn.commit();
      }
#pragma omp critical
      {
        auto ortewtxn = lmdb::txn::begin(db.orteenv());
        auto ortewdbi = lmdb::dbi::open(ortewtxn);
        string rtekey = tfm.key() + "." + inew.key();
        ortewdbi.put(ortewtxn, rtekey.c_str(), prodsetstr.substr(1).c_str(),
            MDB_NOOVERWRITE);
        ortewtxn.commit();
      }
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
      while (itfmcsr.get(itfmkey, itfmval, MDB_NEXT)) {
        Tfm tfm;
        if (!tfm.init(itfmkey, itfmval)) continue;
        clog << itfmkey << endl;

        // for each new smiles
        auto inewrtxn = lmdb::txn::begin(db.inewenv(), nullptr, MDB_RDONLY);
        auto inewcsr = lmdb::cursor::open(inewrtxn, lmdb::dbi::open(inewrtxn));
        string inewkey, inewval;
        Smi inew;
#pragma omp parallel
#pragma omp single nowait
        while (inewcsr.get(inewkey, inewval, MDB_NEXT) && 
            inew.init(inewkey, inewval, tfm))
#pragma omp task firstprivate(tfm, inew), shared(db)
        {
          // apply transform to new smiles
          apply_transform_to_smiles(tfm, inew, db);
        }
        inewrtxn.abort();
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
      "<itfmdb> <inewdb> <ismidb> <irtedb> <onewdb> <osmidb> <ortedb>",
      argv[0]);
  gflags::SetUsageMessage(usage);
  gflags::SetVersionString(PACKAGE_VERSION);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 8) {
    std::cout << usage << std::endl;
    return EXIT_FAILURE;
  }
  return chemstgen::chemstgen(argc, argv);
}
