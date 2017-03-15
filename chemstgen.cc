#include "config.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <libgen.h>
#include <unistd.h>
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
#include "crypto_hash.h"
#include "lmdb++.h"
#include "chemstgen.h"

namespace {

  using namespace std;
  using namespace chemstgen;
  using namespace Helium::Chemist;

  bool overwrite = false;  // overwrite new value for a duplicate key
  uint64_t osmidbmapsize = 1000000;  // output smiles lmdb mapsize in MiB
  uint64_t commitchunksize =  100;  // maximum size of commit chunk
  int32_t maxapply = 1;  // maximum number of applying reaction

  const int keep_mark = 999;
  const regex Xx(R"(Xx)");

  class Db {
    public:
      Db(int argc, char *argv[]):
        itfmenv_(lmdb::env::create()),
        ismienv_(lmdb::env::create()),
        osmienv_(lmdb::env::create()),
        orteenv_(argv[3]) {
#if 0
          std::cout << "argc=" << argc << std::endl;
          std::cout << "argv[0] itfm=" << argv[0] << std::endl;
          std::cout << "argv[1] ismi=" << argv[1] << std::endl;
          std::cout << "argv[2] osmi=" << argv[2] << std::endl;
          std::cout << "argv[3] orte=" << argv[3] << std::endl;
#endif
          // open environments
          opendb(itfmenv_, argv[0], UINT64_C(0), MDB_RDONLY);
          opendb(ismienv_, argv[1], UINT64_C(0), MDB_RDONLY);
          opendb(osmienv_, argv[2], osmidbmapsize);
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
          if (verbose) {
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
        string outputkey = b64urlenc_sha256(output);
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
          string smikey = b64urlenc_sha256(smi);
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
      if (verbose) {
#pragma omp critical
        for (const auto &prodcan: prodset)
          cout << '\t' << prodcan.second << endl;
      }
      const unsigned int put_flags = (overwrite ? 0 : MDB_NOOVERWRITE);
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

          if (++cnt > commitchunksize) {
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

}  // namespace

int main(int argc, char *argv[]) {
  using namespace std;

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <itfm.db> <ismi.db> <osmi.db> <orte.tsv>\n"
    "options: -a <int>   maximum number of applying reaction\n"
    "         -c <size>  maximum size of commit chunk\n"
    "         -o         overwite new value for a duplicate key\n"
    "         -m <size>  output smiles lmdb mapsize in MiB\n"
    "         -v         verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":a:c:om:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'a': { maxapply = stoi(optarg); break; }
        case 'c': { commitchunksize = stoul(optarg); break; }
        case 'o': { overwrite = true; break; }
        case 'm': { osmidbmapsize = stoul(optarg); break; }
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
  if (argc - optind < 4) {
    cout << "too few arguments\n" << usage << flush;
    return EXIT_FAILURE;
  }

  cout.sync_with_stdio(false);
  cerr.sync_with_stdio(false);
  try {
    // open environments
    Db db(argc - optind, argv + optind);

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
