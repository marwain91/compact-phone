#include "Keychain_file.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <spdlog/spdlog.h>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <cstring>

namespace compactphone::platform {

namespace {

constexpr int kSaltSize = 16;
constexpr int kIvSize = 12;       // GCM standard nonce size
constexpr int kTagSize = 16;      // GCM auth tag size
constexpr int kKeySize = 32;      // 256 bits
constexpr const char *kKdfInfo = "CompactPhone:v0.2:keychain";
constexpr const char *kMasterSecret = "compactphone-v0.2-master";

bool deriveKey(const std::string &salt, std::array<uint8_t, kKeySize> &outKey)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx) return false;
    bool ok = false;
    do {
        if (EVP_PKEY_derive_init(ctx) <= 0) break;
        if (EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) <= 0) break;
        if (EVP_PKEY_CTX_set1_hkdf_salt(ctx,
                reinterpret_cast<const uint8_t *>(salt.data()),
                static_cast<int>(salt.size())) <= 0) break;
        if (EVP_PKEY_CTX_set1_hkdf_key(ctx,
                reinterpret_cast<const uint8_t *>(kMasterSecret),
                static_cast<int>(std::strlen(kMasterSecret))) <= 0) break;
        if (EVP_PKEY_CTX_add1_hkdf_info(ctx,
                reinterpret_cast<const uint8_t *>(kKdfInfo),
                static_cast<int>(std::strlen(kKdfInfo))) <= 0) break;
        size_t outLen = outKey.size();
        if (EVP_PKEY_derive(ctx, outKey.data(), &outLen) <= 0) break;
        ok = outLen == outKey.size();
    } while (false);
    EVP_PKEY_CTX_free(ctx);
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

bool FileKeychain::open()
{
    const QString qpath = QString::fromStdString(m_path);
    QFileInfo info(qpath);
    QDir().mkpath(info.absolutePath());

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
    if (!deriveKey(m_salt, key)) return false;

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
    m_store[ref] = password;
    return persist();
}

bool FileKeychain::erase(const std::string &ref)
{
    if (m_store.erase(ref) == 0) return false;
    return persist();
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
    if (!deriveKey(m_salt, key)) return false;

    const auto ct = encrypt(key, plain, iv);
    if (ct.isEmpty()) return false;

    QFile f(QString::fromStdString(m_path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(m_salt.data(), static_cast<int>(m_salt.size()));
    f.write(iv);
    f.write(ct);
    f.close();
    return true;
}

} // namespace compactphone::platform
