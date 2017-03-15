#ifndef CRYPTO_HASH_H__
#define CRYPTO_HASH_H__
#include <openssl/sha.h>
#include <string>

namespace chemstgen {

  std::string b64urlenc_sha256(const std::string &s) {
    const size_t mdsz = SHA256_DIGEST_LENGTH;
    unsigned char md[mdsz + 1];
    SHA256(reinterpret_cast<const unsigned char *>(s.data()), s.length(), md);
    md[mdsz] = '\0';

    const char b64u[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789-_";

    const std::string::size_type bufsz = ((mdsz * 8) + (6 - 1)) / 6;
    std::string buf(bufsz, '\0');

    for (int i = 0, cnt = 0; i < (mdsz + (3 - 1)) / 3; ++i) {
      const unsigned char *c = &md[i * 3];
      for (int j = 0; j < 4; ++j) {
        if (i * 3 + j <= mdsz) {
          buf[cnt++] =
            b64u[(c[j - 1] << (6 - j * 2) | c[j] >> (j * 2 + 2)) & 0x3f];
        }
        else break;
      }
    }
    return buf;
  }

}  // namespace

#endif  // CRYPTO_HASH_H__
