#include "Sentinel/Security/SecretProtector.hpp"
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif
namespace sentinel {
std::vector<std::byte> WindowsDpapiSecretProtector::Protect(std::span<const std::byte> in){
#ifdef _WIN32
    DATA_BLOB src{(DWORD)in.size(),(BYTE*)in.data()}, dst{}; if(!CryptProtectData(&src,L"Sentinel Master Key",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&dst))throw std::runtime_error("CryptProtectData failed");std::vector<std::byte> out(dst.cbData);std::memcpy(out.data(),dst.pbData,dst.cbData);LocalFree(dst.pbData);return out;
#else
    throw std::runtime_error("DPAPI requires Windows");
#endif
}
SecureBuffer WindowsDpapiSecretProtector::Unprotect(std::span<const std::byte> in){
#ifdef _WIN32
    DATA_BLOB src{(DWORD)in.size(),(BYTE*)in.data()}, dst{}; if(!CryptUnprotectData(&src,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&dst))throw std::runtime_error("CryptUnprotectData failed");SecureBuffer out(dst.cbData);std::memcpy(out.Span().data(),dst.pbData,dst.cbData);SecureZeroMemory(dst.pbData,dst.cbData);LocalFree(dst.pbData);return out;
#else
    throw std::runtime_error("DPAPI requires Windows");
#endif
}
}
