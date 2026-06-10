#include "Keychain_file.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <spdlog/spdlog.h>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <array>
#include <cstring>

namespace compactphone::platform {

namespace {

constexpr int kSaltSize = 16;
constexpr int kIvSize = 12;       // GCM standard nonce size
constexpr int kTagSize = 16;      // GCM auth tag size
constexpr int kKeySize = 32;      // 256 bits
constexpr const char *kKdfInfo = "CompactPhone:v0.2:keychain";

// Uses OpenSSL 3.x's EVP_KDF HKDF interface rather than the legacy
// EVP_PKEY_HKDF / EVP_PKEY_derive path. The legacy path crashes inside
// EVP_KEYMGMT_names_do_all on Ubuntu 22.04's libcrypto.so.3 (OpenSSL
// 3.0.2 has a known provider-registry bug when the key-mgmt list isn't
// fully initialised). EVP_KDF was added specifically for KDFs in 3.x
// and doesn't traverse that code path. Same crypto output — HKDF-SHA256
// with the same salt / IKM / info — only the API differs.
bool deriveKey(const std::string &salt,
               const std::string &ikm,
               std::array<uint8_t, kKeySize> &outKey)
{
    if (ikm.empty()) return false;
    EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf) return false;
    EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) return false;

    OSSL_PARAM params[5];
    OSSL_PARAM *p = params;
    char digest[] = "SHA256";
    *p++ = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0);
    *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
            const_cast<char *>(salt.data()), salt.size());
    *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
            const_cast<char *>(ikm.data()), ikm.size());
    *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
            const_cast<char *>(kKdfInfo), std::strlen(kKdfInfo));
    *p = OSSL_PARAM_construct_end();

    const bool ok =
        EVP_KDF_derive(kctx, outKey.data(), outKey.size(), params) > 0;
    EVP_KDF_CTX_free(kctx);
    return ok;
}

QByteArray encrypt(const std::array<uint8_t, kKeySize> &key,
                   const QByteArray &plaintext,
                   const QByteArray &iv)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    QByteArray out;
    out.resize(plaintext.size() + kTagSize);
    int outLen = 0;
    int totalLen = 0;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
            key.data(), reinterpret_cast<const uint8_t *>(iv.constData())) != 1
        || EVP_EncryptUpdate(ctx,
            reinterpret_cast<uint8_t *>(out.data()), &outLen,
            reinterpret_cast<const uint8_t *>(plaintext.constData()),
            static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    totalLen = outLen;
    if (EVP_EncryptFinal_ex(ctx,
            reinterpret_cast<uint8_t *>(out.data()) + totalLen, &outLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    totalLen += outLen;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagSize,
            reinterpret_cast<uint8_t *>(out.data()) + totalLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    EVP_CIPHER_CTX_free(ctx);
    out.resize(totalLen + kTagSize);
    return out;
}

bool decrypt(const std::array<uint8_t, kKeySize> &key,
             const QByteArray &ciphertext,
             const QByteArray &iv,
             QByteArray &outPlain)
{
    if (ciphertext.size() < kTagSize) return false;
    const int ctLen = ciphertext.size() - kTagSize;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    outPlain.resize(ctLen);
    int outLen = 0;
    int totalLen = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
            key.data(), reinterpret_cast<const uint8_t *>(iv.constData())) != 1
        || EVP_DecryptUpdate(ctx,
            reinterpret_cast<uint8_t *>(outPlain.data()), &outLen,
            reinterpret_cast<const uint8_t *>(ciphertext.constData()), ctLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    totalLen = outLen;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagSize,
            const_cast<char *>(ciphertext.constData()) + ctLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    int finalLen = 0;
    const bool ok = EVP_DecryptFinal_ex(ctx,
            reinterpret_cast<uint8_t *>(outPlain.data()) + totalLen, &finalLen) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return false;
    outPlain.resize(totalLen + finalLen);
    return true;
}

} // namespace

FileKeychain::FileKeychain(const std::string &path) : m_path(path) {}

bool FileKeychain::loadOrCreateMasterKey()
{
    const QString keyPath = QString::fromStdString(m_path) + QStringLiteral(".key");
    QFile kf(keyPath);
    if (kf.exists()) {
        if (!kf.open(QIODevice::ReadOnly)) {
            spdlog::error("FileKeychain: master key read failed");
            return false;
        }
        const auto raw = kf.readAll();
        if (raw.size() != kKeySize) {
            spdlog::error("FileKeychain: master key file has wrong size");
            return false;
        }
        m_masterKey.assign(raw.constData(), raw.size());
        return true;
    }

    // First run: mint a random per-install key and store it owner-only.
    // QSaveFile writes to a temp file and renames on commit(), so a failed or
    // interrupted write can never leave a partial .key behind — a partial key
    // would make the keychain blob permanently undecryptable.
    std::array<uint8_t, kKeySize> key{};
    if (RAND_bytes(key.data(), kKeySize) != 1) return false;
    QSaveFile skf(keyPath);
    if (!skf.open(QIODevice::WriteOnly)) {
        spdlog::error("FileKeychain: master key write failed");
        return false;
    }
    // Restrict to owner-only BEFORE writing the secret, so the key bytes are
    // never momentarily readable under the process umask.
    skf.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (skf.write(reinterpret_cast<const char *>(key.data()), kKeySize)
            != kKeySize
        || !skf.commit()) {
        spdlog::error("FileKeychain: master key write failed");
        return false;
    }
    m_masterKey.assign(reinterpret_cast<const char *>(key.data()), kKeySize);
    return true;
}

bool FileKeychain::open()
{
    const QString qpath = QString::fromStdString(m_path);
    QFileInfo info(qpath);
    QDir().mkpath(info.absolutePath());

    if (!loadOrCreateMasterKey()) return false;

    QFile f(qpath);
    if (!f.exists()) {
        m_salt.resize(kSaltSize);
        RAND_bytes(reinterpret_cast<uint8_t *>(m_salt.data()), kSaltSize);
        return persist();
    }
    if (!f.open(QIODevice::ReadOnly)) {
        spdlog::error("FileKeychain::open read failed");
        return false;
    }
    const auto blob = f.readAll();
    if (blob.size() < kSaltSize + kIvSize + kTagSize) return false;
    m_salt.assign(blob.constData(), kSaltSize);
    const QByteArray iv = blob.mid(kSaltSize, kIvSize);
    const QByteArray ct = blob.mid(kSaltSize + kIvSize);

    std::array<uint8_t, kKeySize> key{};
    if (!deriveKey(m_salt, m_masterKey, key)) return false;

    QByteArray plain;
    if (!decrypt(key, ct, iv, plain)) return false;

    const auto doc = QJsonDocument::fromJson(plain);
    if (!doc.isObject()) return false;
    const auto obj = doc.object();
    m_store.clear();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        m_store[it.key().toStdString()] = it.value().toString().toStdString();
    }
    return true;
}

std::optional<std::string> FileKeychain::get(const std::string &ref)
{
    auto it = m_store.find(ref);
    if (it == m_store.end()) return std::nullopt;
    return it->second;
}

bool FileKeychain::set(const std::string &ref, const std::string &password)
{
    // Roll back the in-memory store if the write fails, so a later unrelated
    // persist() can't silently flush a value the caller was told was rejected.
    const auto it = m_store.find(ref);
    const bool existed = it != m_store.end();
    const std::string previous = existed ? it->second : std::string();
    m_store[ref] = password;
    if (persist()) return true;
    if (existed) {
        m_store[ref] = previous;
    } else {
        m_store.erase(ref);
    }
    return false;
}

bool FileKeychain::erase(const std::string &ref)
{
    const auto it = m_store.find(ref);
    if (it == m_store.end()) return false;
    const std::string previous = it->second;
    m_store.erase(it);
    if (persist()) return true;
    m_store[ref] = previous;
    return false;
}

bool FileKeychain::persist()
{
    QJsonObject obj;
    for (const auto &[k, v] : m_store) {
        obj.insert(QString::fromStdString(k), QString::fromStdString(v));
    }
    const auto plain = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QByteArray iv(kIvSize, 0);
    RAND_bytes(reinterpret_cast<uint8_t *>(iv.data()), kIvSize);

    std::array<uint8_t, kKeySize> key{};
    if (!deriveKey(m_salt, m_masterKey, key)) return false;

    const auto ct = encrypt(key, plain, iv);
    if (ct.isEmpty()) return false;

    // Write-to-temp + atomic rename: the previous file is replaced only by
    // commit(), and only after every write succeeded. A disk-full error or a
    // crash mid-write therefore can't truncate the only copy of the store —
    // the old truncate-in-place path here destroyed all saved passwords when
    // a write was cut short, while still reporting success.
    QSaveFile f(QString::fromStdString(m_path));
    if (!f.open(QIODevice::WriteOnly)) {
        spdlog::error("FileKeychain::persist open failed");
        return false;
    }
    // Restrict to owner-only on the temp file, before writing ciphertext.
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto saltLen = static_cast<qint64>(m_salt.size());
    if (f.write(m_salt.data(), saltLen) != saltLen
        || f.write(iv) != iv.size()
        || f.write(ct) != ct.size()) {
        spdlog::error("FileKeychain::persist write failed");
        return false;
    }
    if (!f.commit()) {
        spdlog::error("FileKeychain::persist commit failed");
        return false;
    }
    return true;
}

} // namespace compactphone::platform
