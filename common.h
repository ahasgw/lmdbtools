#ifndef COMMON_H__
#define COMMON_H__
#include <iostream>
#include <string>
#include "lmdb++.h"

namespace {

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

}  // namespace

#endif  // COMMON_H__
