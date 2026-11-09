// Minimal implementations of the Sysutils free functions used by
// UnicodeString. They keep the standalone parser test free of the full core
// (which would drag in configuration, logging and the Far plugin).

#include <vcl.h>

#include <cwchar>
#include <string>

namespace Sysutils
{
namespace
{
bool IsSpace(wchar_t Ch)
{
  return (Ch == L' ') || (Ch == L'\t') || (Ch == L'\r') || (Ch == L'\n');
}
} // namespace

UnicodeString Trim(const UnicodeString & Str)
{
  int32_t First = 1;
  int32_t Last = Str.Length();
  while ((First <= Last) && IsSpace(Str[First]))
  {
    ++First;
  }
  while ((Last >= First) && IsSpace(Str[Last]))
  {
    --Last;
  }
  return (First <= Last) ? Str.SubString(First, Last - First + 1) : UnicodeString();
}

UnicodeString TrimLeft(const UnicodeString & Str)
{
  int32_t First = 1;
  while ((First <= Str.Length()) && IsSpace(Str[First]))
  {
    ++First;
  }
  return Str.SubString(First, Str.Length() - First + 1);
}

UnicodeString TrimRight(const UnicodeString & Str)
{
  int32_t Last = Str.Length();
  while ((Last >= 1) && IsSpace(Str[Last]))
  {
    --Last;
  }
  return Str.SubString(1, Last);
}

UnicodeString UpperCase(const UnicodeString & Str)
{
  std::wstring Result(Str.c_str(), static_cast<size_t>(Str.Length()));
  if (!Result.empty())
  {
    ::CharUpperBuffW(&Result[0], static_cast<DWORD>(Result.size()));
  }
  return UnicodeString(Result.c_str(), static_cast<int32_t>(Result.size()));
}

UnicodeString LowerCase(const UnicodeString & Str)
{
  std::wstring Result(Str.c_str(), static_cast<size_t>(Str.Length()));
  if (!Result.empty())
  {
    ::CharLowerBuffW(&Result[0], static_cast<DWORD>(Result.size()));
  }
  return UnicodeString(Result.c_str(), static_cast<int32_t>(Result.size()));
}

bool IsDelimiter(const UnicodeString & Delimiters, const UnicodeString & Str, int32_t Index)
{
  if ((Index < 1) || (Index > Str.Length()))
  {
    return false;
  }
  return Delimiters.Pos(Str[Index]) > 0;
}

int32_t LastDelimiter(const UnicodeString & Delimiters, const UnicodeString & Str)
{
  for (int32_t Index = Str.Length(); Index >= 1; --Index)
  {
    if (Delimiters.Pos(Str[Index]) > 0)
    {
      return Index;
    }
  }
  return 0;
}

int32_t AnsiCompare(const UnicodeString & Str1, const UnicodeString & Str2)
{
  const int32_t Result = ::CompareStringW(LOCALE_USER_DEFAULT, 0,
    Str1.c_str(), Str1.Length(), Str2.c_str(), Str2.Length());
  return Result - 2;
}

int32_t AnsiCompareIC(const UnicodeString & Str1, const UnicodeString & Str2)
{
  const int32_t Result = ::CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
    Str1.c_str(), Str1.Length(), Str2.c_str(), Str2.Length());
  return Result - 2;
}

int32_t StrToIntDef(const UnicodeString & Value, int32_t DefVal)
{
  const wchar_t * const Start = Value.c_str();
  if ((Start == nullptr) || (*Start == L'\0'))
  {
    return DefVal;
  }
  wchar_t * End = nullptr;
  const long Result = std::wcstol(Start, &End, 10);
  if ((End == Start) || (End == nullptr) || (*End != L'\0'))
  {
    return DefVal;
  }
  return static_cast<int32_t>(Result);
}

UnicodeString StringOfChar(wchar_t Ch, int32_t Len)
{
  return UnicodeString(Len, Ch);
}
} // namespace Sysutils
