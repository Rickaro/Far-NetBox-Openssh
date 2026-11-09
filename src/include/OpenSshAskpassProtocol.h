
#pragma once

#include <cstdint>

// Wire protocol between NetBoxAskpass.exe and the plugin. Fixed-size,
// UTF-16 text (wchar_t on Windows), so both sides can use simple blocking
// ReadFile/WriteFile calls.

namespace OpenSshAskpass
{
constexpr uint32_t Magic = 0x4E424153; // 'NBAS'
constexpr uint32_t TextCapacity = 1024; // characters including terminator

enum class RequestType : uint32_t
{
  Secret = 0,  // password or key passphrase
  Confirm = 1, // host key yes/no question
};

#pragma pack(push, 1)
struct Request
{
  uint32_t Signature;
  uint32_t Type;
  uint64_t Token;
  wchar_t Prompt[TextCapacity];
};

struct Response
{
  uint32_t Signature;
  uint32_t Length; // characters, excluding terminator
  wchar_t Answer[TextCapacity];
};
#pragma pack(pop)
} // namespace OpenSshAskpass
