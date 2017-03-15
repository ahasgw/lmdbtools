#ifndef CHEMSTGEN_H__
#define CHEMSTGEN_H__
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/smiles.h>
#include <Helium/chemist/smirks.h>
#include "lmdb++.h"

namespace chemstgen {

  bool verbose = false;  // verbose output

  volatile std::sig_atomic_t signal_raised = 0;

  /// use with OMP_CANCELLATION=true
  class StSetSignalHandler {
    public:
      StSetSignalHandler()  {
        std::signal(SIGINT,  handler);  // ?= SIG_ERR
      }
      ~StSetSignalHandler() {
        std::signal(SIGINT,  SIG_DFL);
      }
      operator bool() volatile { return chemstgen::signal_raised; }
    private:
      static void handler(int signum) {
        chemstgen::signal_raised = 1;
#pragma omp critical
        std::cout << "caught signal SIGINT" << std::endl;
      }
  };

  class Tfm {
    private:
      uint_fast16_t     id_;
      uint_fast16_t     multiplier_;
      std::string       main_smirks_str_;
      std::string       repldict_str_;
      std::vector<std::vector<std::string>> repldict_;
    public:
      Tfm() {}
      Tfm(const std::string &key, const std::string &val) {
        init(key, val);
      }
      Tfm(const std::string &line) { init(line); }
      ~Tfm() {}
    public:
      bool init(const std::string &key, const std::string &val) {
        return init(key + '\t' + val + '\n');
      }
      bool init(const std::string &line) {
        using namespace std;
        const regex tfmrulepat(R"(^(\d+)\s+(\d+)\s+(\S+)(\s+(\S.*?))?\s*$)");
        smatch match;
        if (!regex_match(line, match, tfmrulepat))
          return false;

        // load to string
        id_ = stoi(match[1]);
        multiplier_ = stoi(match[2]);
        main_smirks_str_ = match[3];
        if (match[5].matched) repldict_str_ = match[5];

        if (verbose) {
#pragma omp critical
          cout << id_ << '\n' << main_smirks_str_ << endl;
        }

        // init main smirks
        Helium::Chemist::Smirks main_smirks;
        if (!main_smirks.init(main_smirks_str_)) {
          if (verbose) {
            cerr << "error: cannot read smirks: " << main_smirks_str_ << endl;
          }
          return false;
        }

        // replace dict
        const regex repldictpat(R"(\S+>>\S+)");
        auto rd_begin = sregex_token_iterator(repldict_str_.begin(),
            repldict_str_.end(), repldictpat);
        auto rd_end = sregex_token_iterator();
        for (auto rdi = rd_begin; rdi != rd_end; ++rdi) {
          string rd = *rdi;
          const regex replpat(R"((\S+>>)(\S+))");
          smatch match;
          if (!regex_match(rd, match, replpat))
            return false;
          string pfx = match[1];
          string rhs = match[2];
          vector<std::string> dictvec;
          const regex or_delim(R"(\|\|)");
          auto dict_begin = sregex_token_iterator(rhs.begin(), rhs.end(),
              or_delim, -1);
          auto dict_end = sregex_token_iterator();
          for (auto di = dict_begin; di != dict_end; ++di) {
            std::string dict = pfx + string(*di);
            if (verbose) {
#pragma omp critical
              cout << "  " << dict << endl;
            }
            Helium::Chemist::Smirks smirks;
            if (!smirks.init(dict)) {
              if (verbose) {
#pragma omp critical
                cerr << "error: cannot read smirks: " << dict << endl;
              }
              return false;
            }
            dictvec.emplace_back(dict);
          }
          repldict_.emplace_back(dictvec);
        }

        if (verbose)
#pragma omp critical
        {
          for (int i = 0; i < repldict_.size(); ++i) {
            cout << " " << repldict_[i].size();
          } cout << endl;
        }
        return true;
      }

      std::string id() const {
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(6) << id_;
        return oss.str();
      }
      uint_fast16_t multiplier() const { return multiplier_; }
      const std::string &main_smirks_str() const { return main_smirks_str_; }
      const std::vector<std::vector<std::string>> &repldict() const {
        return repldict_;
      }

      friend std::ostream &operator<<(std::ostream &os, const Tfm &tfm) {
        using namespace std;
        os << tfm.id() << '\t' << tfm.multiplier() << '\t'
          << tfm.main_smirks_str_;
        if (!tfm.repldict_str_.empty()) os << '\t' << tfm.repldict_str_;
        return os;
      }
      friend std::istream &operator>>(std::istream &is, Tfm &tfm) {
        std::string line;
        if (getline(is, line)) tfm.init(line);
        return is;
      }
  };

  class Smi {
    public:
      Smi(): multiplier_(0) {}
      ~Smi() {}
    public:
      bool init(const std::string &key, const std::string &val) {
        using namespace std;
        key_ = key;
        smiles_ = val;
        return multiply(1);
      }
      bool multiply(uint_fast16_t m = 1) {
        using namespace std;
        string multi_smiles = smiles_;
        string tail("." + smiles_);
        if (m != multiplier_) {
          for (uint_fast16_t i = 1; i < m; ++i) {
            multi_smiles.append(tail);
          }
          Helium::Chemist::Smiles SMILES;
          if (!SMILES.read(multi_smiles, multi_mol_)) {
            if (verbose) {
#pragma omp critical
              cerr << "error: cannot read smiles: " << key_ << endl;
            }
            return false;
          }
          multiplier_ = m;
        }
        return true;
      }

      const Helium::Chemist::Molecule &multi_mol(uint_fast16_t multiplier) {
        multiply(multiplier);
        return multi_mol_;
      }
      const std::string &smiles() const { return smiles_; }
      const std::string &key() const { return key_; }

    private:
      std::string key_;
      std::string smiles_;
      uint_fast16_t multiplier_;
      Helium::Chemist::Molecule multi_mol_;
  };

  inline void check_duplicates(lmdb::dbi &dbi, lmdb::txn &txn,
      const std::string &key, const std::string &val) {
    using namespace std;
    string msg = "duplicate key: " + key;
    lmdb::val keyval(key);
    lmdb::val oldval;
    dbi.get(txn, keyval, oldval);
    if (val != string(oldval.data(), oldval.size())) {
      msg += " collision: '" + string(oldval.data()) + "' and '" + val + "'";
    }
#pragma omp critical
    cerr << msg << endl;
  }

}  // namespace chemstgen

#endif  // CHEMSTGEN_H__
