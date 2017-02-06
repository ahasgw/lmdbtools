#pragma once
#include <string>
#include <cryptopp/cryptlib.h>
#include <cryptopp/sha.h>
#include <cryptopp/ripemd.h>
#include <cryptopp/tiger.h>
#include <cryptopp/whrlpool.h>
#include <cryptopp/base32.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>

namespace chemstgen {

  template <typename Hash, typename Encoder>
  std::string cryptopp_hash(const std::string &str) {
    CryptoPP::SecByteBlock sbb(Hash::DIGESTSIZE);
    const byte *str_ptr = reinterpret_cast<const byte *>(str.data());
    Hash().CalculateDigest(sbb, str_ptr, str.size());
    std::string out;
    CryptoPP::StringSource ss(sbb, sbb.size(), true,
        new Encoder(new CryptoPP::StringSink(out), false));
    return out;
  }

}  // namespace
