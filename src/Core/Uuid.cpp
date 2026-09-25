#include "Sentinel/Core/Types.hpp"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace sentinel {
namespace {
int hexval(char c){ if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; if(c>='A'&&c<='F') return c-'A'+10; return -1; }
}
Uuid Uuid::Random(){
    std::array<std::byte,16> b{};
#ifdef _WIN32
    if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(b.data()),static_cast<ULONG>(b.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
        throw std::runtime_error("BCryptGenRandom failed");
#else
    std::random_device rd; for(auto &x:b) x=std::byte(rd() & 0xff);
#endif
    b[6]=std::byte((std::to_integer<unsigned>(b[6]) & 0x0f) | 0x40);
    b[8]=std::byte((std::to_integer<unsigned>(b[8]) & 0x3f) | 0x80);
    return Uuid{b};
}
std::string Uuid::ToString() const{
    static constexpr int groups[]{4,2,2,2,6};
    std::ostringstream os; os<<std::hex<<std::setfill('0'); size_t p=0;
    for(int g=0;g<5;++g){ if(g) os<<'-'; for(int i=0;i<groups[g];++i) os<<std::setw(2)<<std::to_integer<unsigned>(bytes_[p++]); }
    return os.str();
}
std::optional<Uuid> Uuid::Parse(std::string_view s){
    std::string compact; compact.reserve(32); for(char c:s) if(c!='-') compact.push_back(c); if(compact.size()!=32) return std::nullopt;
    std::array<std::byte,16> b{}; for(size_t i=0;i<16;++i){ int h=hexval(compact[i*2]), l=hexval(compact[i*2+1]); if(h<0||l<0) return std::nullopt; b[i]=std::byte((h<<4)|l); }
    return Uuid{b};
}
std::string Hash256::ToHex() const{ static const char* h="0123456789abcdef"; std::string out; out.resize(64); for(size_t i=0;i<32;++i){auto v=std::to_integer<unsigned>(bytes[i]);out[2*i]=h[v>>4];out[2*i+1]=h[v&15];} return out; }
std::optional<Hash256> Hash256::FromHex(std::string_view s){ if(s.size()!=64)return std::nullopt; Hash256 x; for(size_t i=0;i<32;++i){int a=hexval(s[2*i]),b=hexval(s[2*i+1]);if(a<0||b<0)return std::nullopt;x.bytes[i]=std::byte((a<<4)|b);}return x; }
Timestamp NowUtc(){ return std::chrono::system_clock::now(); }
std::string ToIso8601Utc(Timestamp tp){
    auto t=std::chrono::system_clock::to_time_t(tp); std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm,&t);
#else
    gmtime_r(&t,&tm);
#endif
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count()%1000000;
    char buf[64]; std::snprintf(buf,sizeof(buf),"%04d-%02d-%02dT%02d:%02d:%02d.%06lldZ",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec,(long long)us); return buf;
}
}
