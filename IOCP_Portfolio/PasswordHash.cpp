#include "PasswordHash.h"

#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")

namespace
{
    std::string BytesToHex(const std::vector<BYTE> &bytes)
    {
        std::ostringstream oss;
        for (BYTE b : bytes)
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        return oss.str();
    }

    std::vector<BYTE> HexToBytes(const std::string &hex)
    {
        std::vector<BYTE> bytes(hex.size() / 2);
        for (size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<BYTE>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
        return bytes;
    }

    std::vector<BYTE> Sha256(const std::string &input)
    {
        BCRYPT_ALG_HANDLE hAlg = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        std::vector<BYTE> result;

        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            return result;

        DWORD hashObjLen = 0, hashLen = 0, cbData = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hashObjLen), sizeof(DWORD), &cbData, 0);
        BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen), sizeof(DWORD), &cbData, 0);

        std::vector<BYTE> hashObj(hashObjLen);
        result.resize(hashLen);

        if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjLen, nullptr, 0, 0) >= 0)
        {
            BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char *>(input.data())), static_cast<ULONG>(input.size()), 0);
            BCryptFinishHash(hHash, result.data(), hashLen, 0);
            BCryptDestroyHash(hHash);
        }
        else
        {
            result.clear();
        }

        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }
}

namespace PasswordHash
{
    std::string GenerateSaltHex()
    {
        std::vector<BYTE> salt(16);
        BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return BytesToHex(salt);
    }

    std::string Hash(const std::string &password, const std::string &saltHex)
    {
        std::vector<BYTE> saltBytes = HexToBytes(saltHex);
        std::string salted(reinterpret_cast<const char *>(saltBytes.data()), saltBytes.size());
        salted += password;

        return BytesToHex(Sha256(salted));
    }

    bool Verify(const std::string &password, const std::string &saltHex, const std::string &expectedHashHex)
    {
        return Hash(password, saltHex) == expectedHashHex;
    }
}
