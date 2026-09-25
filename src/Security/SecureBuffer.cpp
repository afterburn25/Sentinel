#include "Sentinel/Security/SecureBuffer.hpp"
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif
namespace sentinel {
SecureBuffer::SecureBuffer(size_t size):data_(size){}
SecureBuffer::SecureBuffer(std::span<const std::byte> source):data_(source.begin(),source.end()){}
SecureBuffer::~SecureBuffer(){Wipe();}
SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept:data_(std::move(other.data_)){}
SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept{ if(this!=&other){Wipe();data_=std::move(other.data_);}return *this; }
void SecureBuffer::Wipe() noexcept{
    if(data_.empty())return;
#ifdef _WIN32
    SecureZeroMemory(data_.data(),data_.size());
#else
    volatile std::byte* p=data_.data(); for(size_t i=0;i<data_.size();++i)p[i]=std::byte{0};
#endif
}
}
