//
//  AttachmentCrypto.cpp
//  MailSync
//
//  See AttachmentCrypto.hpp for the on-disk format and design rationale.
//

#include "AttachmentCrypto.hpp"
#include "MailUtils.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef __APPLE__
#include <cstdlib> // arc4random_buf
#include <CommonCrypto/CommonCrypto.h>
// CommonCrypto's AES-GCM one-shot calls live in <CommonCrypto/CommonCryptorSPI.h>,
// which Apple ships only inside Xcode (not the Command Line Tools SDK). The
// symbols themselves are part of libcommonCrypto (libSystem), stable since
// macOS 10.13. We forward-declare the two prototypes we need so the macOS
// build does not depend on the SPI header being present — keeping the
// "CommonCrypto, zero new dependencies" property of ticket 49b's decision A.
extern "C" {
CCCryptorStatus CCCryptorGCMOneshotEncrypt(
    CCAlgorithm alg, const void * key, size_t keyLength,
    const void * iv, size_t ivLen,
    const void * aData, size_t aDataLen,
    const void * dataIn, size_t dataInLength,
    void * cipherOut, void * tagOut, size_t tagLength);
CCCryptorStatus CCCryptorGCMOneshotDecrypt(
    CCAlgorithm alg, const void * key, size_t keyLength,
    const void * iv, size_t ivLen,
    const void * aData, size_t aDataLen,
    const void * dataIn, size_t dataInLength,
    void * dataOut, const void * tagIn, size_t tagLength);
}
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#endif

using namespace std;

namespace {

const char    MAGIC[4]       = {'A', 'E', 'N', 'C'};
const uint8_t FORMAT_VERSION = 1;
const size_t  NONCE_LEN      = 12;
const size_t  TAG_LEN        = 16;
const size_t  KEY_LEN        = 32;
const size_t  HEADER_LEN     = 4 + 1 + NONCE_LEN; // magic + version + nonce
const char    HKDF_INFO[]    = "actuna-attachment-v1";

// HMAC-SHA256 → 32-byte digest.
void hmacSha256(const uint8_t * key, size_t keyLen,
                const uint8_t * msg, size_t msgLen, uint8_t out[32]) {
#ifdef __APPLE__
    CCHmac(kCCHmacAlgSHA256, key, keyLen, msg, msgLen, out);
#else
    unsigned int outLen = 32;
    HMAC(EVP_sha256(), key, (int)keyLen, msg, msgLen, out, &outLen);
#endif
}

void randomBytes(uint8_t * buf, size_t len) {
#ifdef __APPLE__
    arc4random_buf(buf, len);
#else
    if (RAND_bytes(buf, (int)len) != 1) {
        throw runtime_error("AttachmentCrypto: RAND_bytes failed");
    }
#endif
}

// HKDF-SHA256 with an empty salt (RFC 5869 — an absent salt is HashLen
// zero bytes; Node's crypto.hkdfSync behaves the same, so this matches
// deriveAttachmentKey() in attachment-crypto.ts). L = 32 == HashLen, so
// expand is a single block.
void hkdfSha256(const uint8_t * ikm, size_t ikmLen,
                const char * info, size_t infoLen, uint8_t out[32]) {
    uint8_t zeroSalt[32] = {0};
    uint8_t prk[32];
    hmacSha256(zeroSalt, sizeof(zeroSalt), ikm, ikmLen, prk); // extract

    vector<uint8_t> t1Input(infoLen + 1);
    if (infoLen > 0) {
        memcpy(t1Input.data(), info, infoLen);
    }
    t1Input[infoLen] = 0x01;
    hmacSha256(prk, sizeof(prk), t1Input.data(), t1Input.size(), out); // expand
    memset(prk, 0, sizeof(prk));
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The process-wide attachment key. DBKey arrives as the 64-hex
// ACTUNA_DB_KEY env var (mailsync-process.ts); it MUST be decoded back to
// raw 32 bytes before HKDF so the derived key matches 49a bit-for-bit.
const uint8_t * attachmentKey() {
    static uint8_t key[KEY_LEN];
    static bool ready = false;
    if (ready) {
        return key;
    }

    string hex = MailUtils::getEnvUTF8("ACTUNA_DB_KEY");
    if (hex.length() != 64) {
        throw runtime_error("AttachmentCrypto: ACTUNA_DB_KEY must be 64 hex chars");
    }
    uint8_t dbKey[KEY_LEN];
    for (size_t i = 0; i < KEY_LEN; i++) {
        int hi = hexVal(hex[2 * i]);
        int lo = hexVal(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            throw runtime_error("AttachmentCrypto: ACTUNA_DB_KEY contains non-hex char");
        }
        dbKey[i] = (uint8_t)((hi << 4) | lo);
    }
    hkdfSha256(dbKey, KEY_LEN, HKDF_INFO, sizeof(HKDF_INFO) - 1, key);
    memset(dbKey, 0, sizeof(dbKey));
    ready = true;
    return key;
}

// AES-256-GCM encrypt. `out` receives `plainLen` ciphertext bytes; `tag`
// receives the 16-byte authentication tag. No AAD.
void aesGcmEncrypt(const uint8_t * key, const uint8_t * nonce,
                   const uint8_t * plain, size_t plainLen,
                   uint8_t * out, uint8_t tag[16]) {
#ifdef __APPLE__
    CCCryptorStatus s = CCCryptorGCMOneshotEncrypt(
        kCCAlgorithmAES, key, KEY_LEN, nonce, NONCE_LEN,
        NULL, 0, plain, plainLen, out, tag, TAG_LEN);
    if (s != kCCSuccess) {
        throw runtime_error("AttachmentCrypto: GCM encrypt failed");
    }
#else
    EVP_CIPHER_CTX * ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        throw runtime_error("AttachmentCrypto: EVP_CIPHER_CTX_new failed");
    }
    int len = 0, finalLen = 0;
    bool ok =
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)NONCE_LEN, NULL) == 1 &&
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) == 1 &&
        EVP_EncryptUpdate(ctx, out, &len, plain, (int)plainLen) == 1 &&
        EVP_EncryptFinal_ex(ctx, out + len, &finalLen) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, (int)TAG_LEN, tag) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        throw runtime_error("AttachmentCrypto: GCM encrypt failed");
    }
#endif
}

// AES-256-GCM decrypt. Returns false on tag mismatch (tampered/wrong key).
bool aesGcmDecrypt(const uint8_t * key, const uint8_t * nonce,
                   const uint8_t * cipher, size_t cipherLen,
                   const uint8_t tag[16], uint8_t * out) {
#ifdef __APPLE__
    CCCryptorStatus s = CCCryptorGCMOneshotDecrypt(
        kCCAlgorithmAES, key, KEY_LEN, nonce, NONCE_LEN,
        NULL, 0, cipher, cipherLen, out, tag, TAG_LEN);
    return s == kCCSuccess;
#else
    EVP_CIPHER_CTX * ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        throw runtime_error("AttachmentCrypto: EVP_CIPHER_CTX_new failed");
    }
    int len = 0, finalLen = 0;
    bool ok =
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)NONCE_LEN, NULL) == 1 &&
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) == 1 &&
        EVP_DecryptUpdate(ctx, out, &len, cipher, (int)cipherLen) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, (int)TAG_LEN, (void *)tag) == 1;
    bool authOk = ok && EVP_DecryptFinal_ex(ctx, out + len, &finalLen) == 1;
    EVP_CIPHER_CTX_free(ctx);
    return authOk;
#endif
}

} // namespace

string AttachmentCrypto::encrypt(const char * plain, size_t length) {
    const uint8_t * key = attachmentKey();

    uint8_t nonce[NONCE_LEN];
    randomBytes(nonce, NONCE_LEN);

    string out;
    out.resize(HEADER_LEN + length + TAG_LEN);
    uint8_t * p = reinterpret_cast<uint8_t *>(&out[0]);

    memcpy(p, MAGIC, 4);
    p[4] = FORMAT_VERSION;
    memcpy(p + 5, nonce, NONCE_LEN);

    uint8_t tag[TAG_LEN];
    aesGcmEncrypt(key, nonce, reinterpret_cast<const uint8_t *>(plain), length,
                  p + HEADER_LEN, tag);
    memcpy(p + HEADER_LEN + length, tag, TAG_LEN);
    return out;
}

string AttachmentCrypto::decrypt(const char * stored, size_t length) {
    // Legacy pre-49 plaintext: no AENC header → return unchanged.
    if (length < HEADER_LEN + TAG_LEN || memcmp(stored, MAGIC, 4) != 0) {
        return string(stored, length);
    }
    const uint8_t * p = reinterpret_cast<const uint8_t *>(stored);
    if (p[4] != FORMAT_VERSION) {
        throw runtime_error("AttachmentCrypto: unsupported format version");
    }
    const uint8_t * nonce  = p + 5;
    const uint8_t * cipher = p + HEADER_LEN;
    size_t cipherLen       = length - HEADER_LEN - TAG_LEN;
    const uint8_t * tag    = p + length - TAG_LEN;

    string out;
    out.resize(cipherLen);
    uint8_t * outPtr = cipherLen ? reinterpret_cast<uint8_t *>(&out[0]) : NULL;
    if (!aesGcmDecrypt(attachmentKey(), nonce, cipher, cipherLen, tag, outPtr)) {
        throw runtime_error("AttachmentCrypto: GCM tag mismatch (tampered or wrong key)");
    }
    return out;
}
