#include "config.h"
#include <cerrno>
#include <cstdlib>
#include <libgen.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <Helium/chemist/molecule.h>
#include <Helium/chemist/smiles.h>
#include "crypto_hash.h"
#include "lmdb++.h"
#include "chemstgen.h"

int main(int argc, char *argv[]) {
  using namespace std;
  using namespace chemstgen;
  using namespace Helium::Chemist;

  bool genkey = false;  // generate hash key
  bool canonicalize = false;  // canonicalize smiles
  bool singlecomponentonly = false;  // select single component smiles only
  bool removecomment = false;  // remove comment line beginning with '#'
  bool overwrite = false;  // overwrite new value for a duplicate key
  uint64_t mapsize = 10000;  // lmdb map size in MiB

  string progname = basename(argv[0]);
  string usage = "usage: " + progname +
    " [options] <dbname> <datafile>\n"
    "options: -k         generate hash key\n"
    "         -c         canonicalize smiles\n"
    "         -1         select single component smiles only\n"
    "         -r         remove comment line beginning with '#'\n"
    "         -o         overwrite new value for a duplicate key\n"
    "         -m <size>  lmdb map size in MiB (" + to_string(mapsize) + ")\n"
    "         -v         verbose output\n"
    ;
  for (opterr = 0;;) {
    int opt = getopt(argc, argv, ":kc1rom:v");
    if (opt == -1) break;
    try {
      switch (opt) {
        case 'k': { genkey = true; break; }
        case 'c': { canonicalize = true; break; }
        case '1': { singlecomponentonly = true; break; }
        case 'r': { removecomment = true; break; }
        case 'o': { overwrite = true; break; }
        case 'm': { mapsize = stoul(optarg); break; }
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
  if (argc - optind < 2) {
    cout << "too few arguments\n" << usage << flush;
    exit(EXIT_FAILURE);
  }

  int oi = optind;
  string odbfname (argv[oi++]);
  string ismifname(argv[oi++]);

  const unsigned int put_flags = (overwrite ? 0 : MDB_NOOVERWRITE);

  try {
    auto env = lmdb::env::create();
    env.set_mapsize(mapsize * 1024UL * 1024UL);
    env.open(odbfname.c_str(), MDB_NOSUBDIR | MDB_NOLOCK);

    auto wtxn = lmdb::txn::begin(env);
    auto dbi  = lmdb::dbi::open(wtxn);

    smatch match;
    ifstream ifs(ismifname);
    if (genkey) {
      Molecule mol;
      Smiles SMILES;
      // smi[ comm]
      const regex pattern(removecomment
          ? R"(^([^#]\S*|)(\s+(.*?)\s*)?$)"
          : R"(^(\S*)(\s+(.*?)\s*)?$)"
          );
      for (string line; getline(ifs, line);) {
        if (regex_match(line, match, pattern)) {
          string val = match[1];  // smi
          if (canonicalize || singlecomponentonly) {
            SMILES.read(val, mol);
            if (singlecomponentonly &&
                (num_connected_components(mol) != 1))
              continue;
            if (canonicalize) {
              make_hydrogens_implicit(mol);
              reset_implicit_hydrogens(mol);
              val = SMILES.writeCanonical(mol);
            }
          }
          string key = b64urlenc_sha256(val);
          if (!dbi.put(wtxn, key.c_str(), val.c_str(), put_flags)) {
            if (verbose) {
              check_duplicates(dbi, wtxn, key, val);
            }
          }
        }
      }
    } else {
      // key smi[ comm]
      const regex pattern(R"(^(\S+)\s(\S*)(\s+(.*?)\s*)?$)");
      for (string line; getline(ifs, line);) {
        if (regex_match(line, match, pattern)) {
          const string &key = match[1];  // hash
          const string &val = match[2];  // smi
          if (!dbi.put(wtxn, key.c_str(), val.c_str(), put_flags)) {
            if (verbose) {
              check_duplicates(dbi, wtxn, key, val);
            }
          }
        }
      }
    }

    MDB_stat st = dbi.stat(wtxn);
    cout << odbfname << '\t' << st.ms_entries << endl;
    wtxn.commit();
  }
  catch (const lmdb::error &e) {
    cerr << e.what() << endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
