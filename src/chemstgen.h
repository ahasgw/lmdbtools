#pragma once
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/smiles.h>
#include <Helium/chemist/smirks.h>
#include <gflags/gflags.h>
#include "lmdb++.h"

DECLARE_bool(verbose);

namespace chemstgen {

  class Tfm {
    public:
      Tfm() {}
      ~Tfm() {}
      bool init(const std::string &key, const std::string &val) {
        using namespace std;
        key_ = key;
#ifdef DEBUG
        tfm_ = val;
#endif // DEBUG
        string smirks, smirks_aux;
        istringstream iss(val);
        if (!(iss >> multiply_ >> smirks)) {
          if (FLAGS_verbose) {
            cerr << "error: cannot read transform: " << key << endl;
          }
          return false;
        }
        if (!smirks_.init(smirks)) {
          if (FLAGS_verbose) {
            cerr << "error: cannot read transform: " << key << endl;
          }
          return false;
        }
        for (string rep; iss >> rep;) {
          const regex LR(R"(^(\S+>>)(\S+)$)");
          smatch LRmatch;
          regex_match(rep, LRmatch, LR);
          replacedict_.emplace_back(pfxsplit(LRmatch[1], LRmatch[2], "||"));
        }
        return true;
      }

      size_t multiply() const { return multiply_; }
      Helium::Chemist::Smirks &smirks() { return smirks_; }
      const std::vector<std::vector<std::string>> replacedict() const {
        return replacedict_;
      }
#ifdef DEBUG
      const std::string &tfm() const { return tfm_; }
#endif // DEBUG
      const std::string &key() const { return key_; }

    private:
      std::string key_;
#ifdef DEBUG
      std::string tfm_;
#endif // DEBUG
      std::vector<std::vector<std::string>> replacedict_;
      Helium::Chemist::Smirks smirks_;
      size_t multiply_;

    private:
      std::vector<std::string> pfxsplit(const std::string &pfx,
          const std::string &s, const std::string &delim) {
        using namespace std;
        vector<string> vs;
        size_t beg = 0;
        for (size_t pos; (pos = s.find(delim, beg)) != string::npos;) {
          vs.emplace_back(pfx + string(s, beg, pos - beg));
          beg = pos + delim.length();
        }
        vs.emplace_back(pfx + string(s, beg, s.size() - beg));
        return vs;
      }
  };

  class Smi {
    public:
      Smi() {}
      ~Smi() {}
      bool init(const std::string &key, const std::string &val,
          const Tfm &tfm) {
        using namespace std;
        key_ = key;
        smiles_ = val;

        string multi_smiles_ = smiles_;
        if (tfm.multiply() > 1) {
          string tail("." + smiles_);
          for (size_t i = 1; i < tfm.multiply(); ++i) {
            multi_smiles_.append(tail);
          }
        }
        Helium::Chemist::Smiles SMILES;
        if (!SMILES.read(multi_smiles_, multi_mol_)) {
          if (FLAGS_verbose) {
            cerr << "error: cannot read smiles: " << key << endl;
          }
          return false;
        }
        return true;
      }

      Helium::Chemist::Molecule &multi_mol() { return multi_mol_; }
      const std::string &smiles() const { return smiles_; }
      const std::string &key() const { return key_; }

    private:
      std::string key_;
      std::string smiles_;
      Helium::Chemist::Molecule multi_mol_;
  };

  inline void check_duplicates(lmdb::dbi &dbi, lmdb::txn &txn,
      const std::string &key, const std::string &val) {
    using namespace std;
    cerr << "duplicate key: " << key;
    lmdb::val keyval(key);
    lmdb::val oldval;
    dbi.get(txn, keyval, oldval);
    if (val != string(oldval.data(), oldval.size())) {
      cerr << " collision: '" << oldval.data() << "' and '" << val << "'";
    }
    cerr << endl;
  }

}  // namespace chemstgen
