
#include <vcl.h>
#pragma hdrstop

#include <Common.h>
#include <Sysutils.hpp>
#include <UnicodeString.hpp>

#include "OpenSshConfig.h"

#include <algorithm>
#include <cwctype>
#include <exception>
#include <set>
#include <string>
#include <vector>

namespace OpenSsh
{
namespace
{
constexpr int32_t MaxIncludeDepth = 10;
constexpr DWORD MaxConfigFileSize = 4 * 1024 * 1024;

std::wstring Trim(const std::wstring & Value)
{
  const wchar_t * const Spaces = L" \t\r\n";
  const size_t First = Value.find_first_not_of(Spaces);
  if (First == std::wstring::npos)
  {
    return std::wstring();
  }
  const size_t Last = Value.find_last_not_of(Spaces);
  return Value.substr(First, Last - First + 1);
}

std::wstring ToLower(const std::wstring & Value)
{
  std::wstring Result(Value);
  for (wchar_t & Ch : Result)
  {
    Ch = static_cast<wchar_t>(std::towlower(Ch));
  }
  return Result;
}

bool EqualsI(const std::wstring & Left, const std::wstring & Right)
{
  return ToLower(Left) == ToLower(Right);
}

std::wstring ToWide(const UnicodeString & Value)
{
  return std::wstring(Value.c_str(), static_cast<size_t>(Value.Length()));
}

UnicodeString FromWide(const std::wstring & Value)
{
  return UnicodeString(Value.c_str(), static_cast<int32_t>(Value.size()));
}

bool IsDirectorySeparator(wchar_t Ch)
{
  return (Ch == L'\\') || (Ch == L'/');
}

std::wstring NormalizePath(const std::wstring & Path)
{
  std::wstring Result;
  Result.reserve(Path.size());
  for (const wchar_t Ch : Path)
  {
    if (IsDirectorySeparator(Ch))
    {
      if (Result.empty() || !IsDirectorySeparator(Result.back()))
      {
        Result.push_back(L'\\');
      }
    }
    else
    {
      Result.push_back(Ch);
    }
  }
  while ((Result.size() > 1) && (Result.back() == L'\\') &&
    !((Result.size() == 3) && (Result[1] == L':')))
  {
    Result.pop_back();
  }
  return Result;
}

bool IsAbsolutePath(const std::wstring & Path)
{
  if (Path.empty())
  {
    return false;
  }
  if ((Path.size() >= 2) && (Path[1] == L':'))
  {
    return true;
  }
  return (Path.size() >= 2) && IsDirectorySeparator(Path[0]) && IsDirectorySeparator(Path[1]);
}

std::wstring CombinePath(const std::wstring & Directory, const std::wstring & Name)
{
  if (Directory.empty())
  {
    return NormalizePath(Name);
  }
  if (IsAbsolutePath(Name))
  {
    return NormalizePath(Name);
  }
  std::wstring Result = Directory;
  if (!Result.empty() && !IsDirectorySeparator(Result.back()))
  {
    Result.push_back(L'\\');
  }
  return NormalizePath(Result + Name);
}

std::wstring ExtractDirectory(const std::wstring & Path)
{
  const size_t Position = Path.find_last_of(L"\\/");
  if (Position == std::wstring::npos)
  {
    return std::wstring();
  }
  return Path.substr(0, Position);
}

std::wstring ExtractFileName(const std::wstring & Path)
{
  const size_t Position = Path.find_last_of(L"\\/");
  if (Position == std::wstring::npos)
  {
    return Path;
  }
  return Path.substr(Position + 1);
}

std::wstring ExtractStem(const std::wstring & Path)
{
  std::wstring Name = ExtractFileName(Path);
  const size_t Position = Name.find_last_of(L'.');
  if ((Position != std::wstring::npos) && (Position > 0))
  {
    Name.erase(Position);
  }
  return Name;
}

bool FileExists(const std::wstring & Path)
{
  const DWORD Attributes = ::GetFileAttributesW(Path.c_str());
  return (Attributes != INVALID_FILE_ATTRIBUTES) && ((Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

bool HasWildcard(const std::wstring & Path)
{
  return (Path.find(L'*') != std::wstring::npos) || (Path.find(L'?') != std::wstring::npos);
}

std::wstring DecodeText(const std::vector<char> & Data)
{
  if (Data.empty())
  {
    return std::wstring();
  }

  size_t Offset = 0;
  UINT CodePage = CP_ACP;
  DWORD Flags = 0;

  if ((Data.size() >= 3) &&
    (static_cast<unsigned char>(Data[0]) == 0xEF) &&
    (static_cast<unsigned char>(Data[1]) == 0xBB) &&
    (static_cast<unsigned char>(Data[2]) == 0xBF))
  {
    Offset = 3;
    CodePage = CP_UTF8;
    Flags = MB_ERR_INVALID_CHARS;
  }
  else if ((Data.size() >= 2) &&
    (static_cast<unsigned char>(Data[0]) == 0xFF) &&
    (static_cast<unsigned char>(Data[1]) == 0xFE))
  {
    const size_t Characters = (Data.size() - 2) / sizeof(wchar_t);
    return std::wstring(
      reinterpret_cast<const wchar_t *>(Data.data() + 2), Characters);
  }
  else
  {
    // Prefer UTF-8, fall back to the ANSI code page.
    CodePage = CP_UTF8;
    Flags = MB_ERR_INVALID_CHARS;
  }

  const char * const Start = Data.data() + Offset;
  const int32_t Size = static_cast<int32_t>(Data.size() - Offset);
  int32_t Length = ::MultiByteToWideChar(CodePage, Flags, Start, Size, nullptr, 0);
  if ((Length == 0) && (Flags != 0))
  {
    CodePage = CP_ACP;
    Flags = 0;
    Length = ::MultiByteToWideChar(CodePage, Flags, Start, Size, nullptr, 0);
  }
  if (Length <= 0)
  {
    return std::wstring();
  }
  std::wstring Result(static_cast<size_t>(Length), L'\0');
  ::MultiByteToWideChar(CodePage, Flags, Start, Size, &Result[0], Length);
  return Result;
}

bool ReadTextFile(const std::wstring & Path, std::wstring & Text, std::wstring & Error)
{
  const HANDLE Handle = ::CreateFileW(Path.c_str(), GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (Handle == INVALID_HANDLE_VALUE)
  {
    Error = L"Cannot open " + Path;
    return false;
  }

  std::vector<char> Data;
  try
  {
    LARGE_INTEGER Size{};
    if (::GetFileSizeEx(Handle, &Size) && (Size.QuadPart > 0))
    {
      const int64_t Reserve = (Size.QuadPart < static_cast<int64_t>(MaxConfigFileSize)) ?
        Size.QuadPart : static_cast<int64_t>(MaxConfigFileSize);
      Data.reserve(static_cast<size_t>(Reserve));
    }
    char Buffer[16 * 1024];
    DWORD Read = 0;
    do
    {
      Read = 0;
      if (!::ReadFile(Handle, Buffer, sizeof(Buffer), &Read, nullptr))
      {
        Error = L"Cannot read " + Path;
        ::CloseHandle(Handle);
        return false;
      }
      if (Read > 0)
      {
        Data.insert(Data.end(), Buffer, Buffer + Read);
        if (Data.size() > MaxConfigFileSize)
        {
          Data.resize(MaxConfigFileSize);
          break;
        }
      }
    }
    while (Read > 0);
  }
  catch (const std::exception &)
  {
    ::CloseHandle(Handle);
    Error = L"Cannot read " + Path;
    return false;
  }

  ::CloseHandle(Handle);
  Text = DecodeText(Data);
  return true;
}

std::vector<std::wstring> SplitLines(const std::wstring & Text)
{
  std::vector<std::wstring> Result;
  size_t Start = 0;
  while (Start <= Text.size())
  {
    size_t End = Text.find(L'\n', Start);
    if (End == std::wstring::npos)
    {
      End = Text.size();
    }
    std::wstring Line = Text.substr(Start, End - Start);
    if (!Line.empty() && (Line.back() == L'\r'))
    {
      Line.pop_back();
    }
    Result.push_back(Line);
    if (End == Text.size())
    {
      break;
    }
    Start = End + 1;
  }
  return Result;
}

// Splits a directive line into a directive name and its raw value, supporting
// the "Key Value", "Key=Value" and "\"Key\" Value" forms.
bool ParseDirective(const std::wstring & Line, std::wstring & Directive, std::wstring & Value)
{
  const std::wstring Trimmed = Trim(Line);
  if (Trimmed.empty() || (Trimmed[0] == L'#'))
  {
    return false;
  }

  size_t Position = 0;
  if (Trimmed[0] == L'"')
  {
    const size_t Closing = Trimmed.find(L'"', 1);
    if (Closing == std::wstring::npos)
    {
      return false;
    }
    Directive = Trimmed.substr(1, Closing - 1);
    Position = Closing + 1;
  }
  else
  {
    while ((Position < Trimmed.size()) &&
      (Trimmed[Position] != L' ') && (Trimmed[Position] != L'\t') && (Trimmed[Position] != L'='))
    {
      ++Position;
    }
    if (Position == 0)
    {
      return false;
    }
    Directive = Trimmed.substr(0, Position);
  }

  while ((Position < Trimmed.size()) &&
    ((Trimmed[Position] == L' ') || (Trimmed[Position] == L'\t') || (Trimmed[Position] == L'=')))
  {
    ++Position;
  }

  Value = Trimmed.substr(Position);
  return !(Directive.empty() || Value.empty());
}

// Splits the value of a directive into tokens using OpenSSH quoting rules.
std::vector<std::wstring> SplitTokens(const std::wstring & Value)
{
  std::vector<std::wstring> Result;
  std::wstring Current;
  wchar_t Quote = L'\0';
  for (size_t Index = 0; Index < Value.size(); ++Index)
  {
    const wchar_t Ch = Value[Index];
    if ((Ch == L'\\') && (Index + 1 < Value.size()))
    {
      const wchar_t Next = Value[Index + 1];
      if ((Next == L'\'') || (Next == L'"') || (Next == L'\\') ||
        ((Quote == L'\0') && ((Next == L' ') || (Next == L'\t'))) || (Next == L'#'))
      {
        Current.push_back(Next);
        ++Index;
        continue;
      }
    }
    if (Quote == L'\0')
    {
      if ((Ch == L' ') || (Ch == L'\t'))
      {
        if (!Current.empty())
        {
          Result.push_back(Current);
          Current.clear();
        }
        continue;
      }
      if (Ch == L'#')
      {
        break;
      }
      if ((Ch == L'\'') || (Ch == L'"'))
      {
        Quote = Ch;
        continue;
      }
    }
    else if (Ch == Quote)
    {
      Quote = L'\0';
      continue;
    }
    Current.push_back(Ch);
  }
  if (!Current.empty())
  {
    Result.push_back(Current);
  }
  return Result;
}

std::wstring SanitizeFolderComponent(const std::wstring & Value)
{
  std::wstring Result;
  Result.reserve(Value.size());
  for (const wchar_t Ch : Value)
  {
    if ((Ch == L'<') || (Ch == L'>') || (Ch == L':') || (Ch == L'"') ||
      (Ch == L'|') || (Ch == L'?') || (Ch == L'*') || (Ch == L'/') ||
      (Ch == L'\\') || (Ch < 0x20))
    {
      Result.push_back(L'_');
    }
    else
    {
      Result.push_back(Ch);
    }
  }
  Result = Trim(Result);
  while (!Result.empty() && (Result.back() == L'.'))
  {
    Result.pop_back();
  }
  if (Result.empty())
  {
    Result = L"_";
  }
  return Result;
}

std::wstring JoinFolder(const std::wstring & Parent, const std::wstring & Child)
{
  if (Parent.empty())
  {
    return Child;
  }
  if (Child.empty())
  {
    return Parent;
  }
  return Parent + L"/" + Child;
}

std::vector<std::wstring> ExpandIncludePath(
  const std::wstring & Path, std::vector<UnicodeString> & Warnings)
{
  std::vector<std::wstring> Result;
  const std::wstring Normalized = NormalizePath(Path);

  if (!HasWildcard(Normalized))
  {
    if (FileExists(Normalized))
    {
      Result.push_back(Normalized);
    }
    else
    {
      Warnings.push_back(UnicodeString(L"Included OpenSSH configuration file not found: ") +
        FromWide(Normalized));
    }
    return Result;
  }

  const std::wstring Directory = ExtractDirectory(Normalized);
  const std::wstring Pattern = ExtractFileName(Normalized);
  if (HasWildcard(Directory) || Directory.empty())
  {
    Warnings.push_back(UnicodeString(L"Unsupported OpenSSH Include pattern: ") + FromWide(Normalized));
    return Result;
  }

  WIN32_FIND_DATAW FindData{};
  const HANDLE FindHandle = ::FindFirstFileW((Directory + L"\\" + Pattern).c_str(), &FindData);
  if (FindHandle == INVALID_HANDLE_VALUE)
  {
    Warnings.push_back(UnicodeString(L"OpenSSH Include pattern matched no files: ") + FromWide(Normalized));
    return Result;
  }

  do
  {
    if (((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) &&
      (FindData.cFileName[0] != L'.'))
    {
      Result.push_back(CombinePath(Directory, FindData.cFileName));
    }
  }
  while (::FindNextFileW(FindHandle, &FindData));
  ::FindClose(FindHandle);
  std::sort(Result.begin(), Result.end(),
    [](const std::wstring & Left, const std::wstring & Right)
    {
      return ToLower(Left) < ToLower(Right);
    });
  return Result;
}

} // namespace

UnicodeString TConfigReader::GetDefaultIncludeBaseDir()
{
  wchar_t Buffer[32768];
  const DWORD BufferSize = static_cast<DWORD>(_countof(Buffer));
  const DWORD Length = ::GetEnvironmentVariableW(L"HOME", Buffer, BufferSize);
  if ((Length > 0) && (Length < BufferSize))
  {
    return FromWide(CombinePath(Buffer, L".ssh"));
  }
  const DWORD ProfileLength = ::GetEnvironmentVariableW(L"USERPROFILE", Buffer, BufferSize);
  if ((ProfileLength > 0) && (ProfileLength < BufferSize))
  {
    return FromWide(CombinePath(Buffer, L".ssh"));
  }
  return UnicodeString();
}

UnicodeString TConfigReader::GetDefaultConfigPath()
{
  const UnicodeString BaseDir = GetDefaultIncludeBaseDir();
  if (BaseDir.IsEmpty())
  {
    return UnicodeString();
  }
  return BaseDir + UnicodeString(L"\\config");
}

TConfigReader::TConfigReader(
  const UnicodeString & ConfigPath, const UnicodeString & IncludeBaseDir) :
  FConfigPath(ConfigPath),
  FIncludeBaseDir(IncludeBaseDir)
{
}

void TConfigReader::Read()
{
  FHosts.clear();
  FWarnings.clear();
  FStack.clear();

  const std::wstring Path = NormalizePath(ToWide(FConfigPath));
  if (!FileExists(Path))
  {
    throw Exception(UnicodeString(L"OpenSSH configuration file not found: ") + FromWide(Path));
  }
  ReadFile(Path, std::wstring());
}

void TConfigReader::ReadFile(
  const std::wstring & Path, const std::wstring & Folder)
{
  const std::wstring Normalized = NormalizePath(Path);

  for (const std::wstring & StackPath : FStack)
  {
    if (EqualsI(StackPath, Normalized))
    {
      FWarnings.push_back(UnicodeString(L"Recursive OpenSSH Include ignored: ") + FromWide(Normalized));
      return;
    }
  }
  if (FStack.size() >= static_cast<size_t>(MaxIncludeDepth))
  {
    FWarnings.push_back(UnicodeString(L"OpenSSH Include depth limit reached: ") + FromWide(Normalized));
    return;
  }

  std::wstring Text;
  std::wstring Error;
  if (!ReadTextFile(Normalized, Text, Error))
  {
    throw Exception(FromWide(Error));
  }

  FStack.push_back(Normalized);

  const std::vector<std::wstring> Lines = SplitLines(Text);
  bool SkippingMatch = false;
  for (size_t Index = 0; Index < Lines.size(); ++Index)
  {
    std::wstring Directive;
    std::wstring Value;
    if (!ParseDirective(Lines[Index], Directive, Value))
    {
      continue;
    }

    if (EqualsI(Directive, L"Match"))
    {
      // Directives of a Match block never declare host aliases.
      SkippingMatch = true;
      continue;
    }

    if (EqualsI(Directive, L"Host"))
    {
      SkippingMatch = false;
      const std::vector<std::wstring> Tokens = SplitTokens(Value);
      for (const std::wstring & Token : Tokens)
      {
        if (Token.empty() || (Token[0] == L'!'))
        {
          continue;
        }
        // Wildcards describe groups of hosts, not a concrete alias.
        if (HasWildcard(Token))
        {
          continue;
        }
        const std::wstring Key = ToLower(Token);
        bool Known = false;
        for (const THostEntry & Existing : FHosts)
        {
          if (ToLower(ToWide(Existing.Host)) == Key)
          {
            Known = true;
            break;
          }
        }
        if (Known)
        {
          continue;
        }

        THostEntry Entry;
        Entry.Host = FromWide(Token);
        Entry.Folder = FromWide(Folder);
        Entry.SourceFile = FromWide(Normalized);
        Entry.Line = static_cast<int32_t>(Index) + 1;
        FHosts.push_back(Entry);
      }
      continue;
    }

    if (EqualsI(Directive, L"Include") && !SkippingMatch)
    {
      const std::wstring Base = ToWide(FIncludeBaseDir);
      std::wstring Home = Base;
      if (EqualsI(ExtractFileName(Base), L".ssh"))
      {
        Home = ExtractDirectory(Base);
      }
      for (const std::wstring & Token : SplitTokens(Value))
      {
        if (Token.empty())
        {
          continue;
        }

        std::wstring IncludePath = Token;
        if ((IncludePath.size() >= 2) && (IncludePath[0] == L'~') && IsDirectorySeparator(IncludePath[1]))
        {
          IncludePath = Home + IncludePath.substr(1);
        }
        if (!IsAbsolutePath(IncludePath))
        {
          IncludePath = CombinePath(Base, IncludePath);
        }
        IncludePath = NormalizePath(IncludePath);

        const std::vector<std::wstring> Files = ExpandIncludePath(IncludePath, FWarnings);
        for (const std::wstring & File : Files)
        {
          const std::wstring ChildFolder = JoinFolder(
            Folder, SanitizeFolderComponent(ExtractStem(File)));
          ReadFile(File, ChildFolder);
        }
      }
      continue;
    }
  }

  FStack.pop_back();
}
} // namespace OpenSsh
