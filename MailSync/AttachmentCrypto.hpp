//
//  AttachmentCrypto.hpp
//  MailSync
//
//  Ticket 49b — attachment at-rest encryption (C++ half).
//
//  Attachment files in `files/<id>/` were stored in plaintext while the
//  SQLCipher database was already encrypted (audit
//  analysis/15-storage-audit-code-verified.md, Podatność 1 — High Risk).
//  This module is the C++ half; the JS half is app/src/attachment-crypto.ts
//  (49a). Both implement the SAME on-disk format so a file written by
//  either side opens on the other:
//
//    [ magic "AENC" (4B) | version u8=1 (1B) | nonce (12B) | ciphertext | GCM tag (16B) ]
//
//  Cipher: AES-256-GCM. The key is HKDF-SHA256 derived ONCE from the
//  SQLCipher DBKey (ACTUNA_DB_KEY env var) — no separate secret to manage,
//  same trust model as the database. Per-file random 96-bit nonce.
//
//  Platform split (ticket 49b decision — "Rekomendacja A"): macOS uses
//  CommonCrypto (mailsync does not link OpenSSL on macOS); Linux/Windows
//  use OpenSSL EVP.
//

#ifndef AttachmentCrypto_hpp
#define AttachmentCrypto_hpp

#include <string>
#include <cstddef>

namespace AttachmentCrypto {

// Encrypt a plaintext attachment buffer into the on-disk AENC format.
// Throws std::runtime_error if the key is unavailable or the cipher fails.
std::string encrypt(const char * plain, size_t length);

// Decrypt an on-disk attachment buffer.
//  - AENC-format input → authenticated decrypt (throws std::runtime_error
//    on a bad GCM tag / wrong key / unsupported version).
//  - non-AENC input → legacy pre-49 plaintext, returned unchanged.
std::string decrypt(const char * stored, size_t length);

}

#endif /* AttachmentCrypto_hpp */
