#include "ContentBuildHash.h"
#include "ContentBuildPaths.h"
#include <openssl/evp.h>
#include <array>
#include <fstream>
#include <memory>

bool ContentBuildHash::Valid(std::string const& hash)
{
    return hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos;
}

bool ContentBuildHash::Calculate(std::filesystem::path const& path, std::string& hash, std::string& error)
{
    using namespace ContentBuildPaths;
    hash.clear();
    try
    {
        RejectLinks(path);
        Require(fs::is_regular_file(path), "MPQ artifact is missing or is not a regular file: " + path.string());
        auto size = fs::file_size(path);
        auto modified = fs::last_write_time(path);
        Require(size > 0, "MPQ artifact is empty");
        std::ifstream input(path, std::ios::binary);
        Require(input.is_open(), "Cannot open MPQ for hashing: " + path.string());
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        Require(context && EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1, "SHA256 initialization failed");
        std::array<char, 65536> buffer;
        std::uintmax_t bytes = 0;
        while (input)
        {
            input.read(buffer.data(), buffer.size());
            auto count = input.gcount();
            if (count > 0)
            {
                bytes += static_cast<std::uintmax_t>(count);
                Require(EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(count)) == 1,
                    "SHA256 update failed");
            }
        }
        Require(input.eof() && !input.bad(), "MPQ read failed during hashing");
        RejectLinks(path);
        Require(bytes == size && fs::file_size(path) == size && fs::last_write_time(path) == modified,
            "MPQ changed during hashing; keep artifacts immutable");
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int length = 0;
        Require(EVP_DigestFinal_ex(context.get(), digest, &length) == 1 && length == 32, "SHA256 finalization failed");
        static char const digits[] = "0123456789abcdef";
        for (unsigned int i = 0; i < length; ++i)
        {
            hash += digits[digest[i] >> 4];
            hash += digits[digest[i] & 15];
        }
        return true;
    }
    catch (std::exception const& exception)
    {
        error = exception.what();
        return false;
    }
}

std::string ContentBuildHash::Bytes(std::vector<std::uint8_t> const& bytes)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest, &length, EVP_sha256(), nullptr) != 1 || length != 32)
        throw std::runtime_error("SHA256 memory digest failed");
    static char const digits[] = "0123456789abcdef";
    std::string hash;
    for (unsigned i = 0; i < length; ++i) { hash += digits[digest[i] >> 4]; hash += digits[digest[i] & 15]; }
    return hash;
}
