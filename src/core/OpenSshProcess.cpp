
#include <vcl.h>
#pragma hdrstop

#include <Common.h>
#include <UnicodeString.hpp>

#include "OpenSshProcess.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace OpenSsh
{
namespace
{
constexpr DWORD ReadChunkLimit = 256 * 1024;
constexpr DWORD PipeBufferSize = 1024 * 1024;

std::wstring ToWide(const UnicodeString & Value)
{
  return std::wstring(Value.c_str(), static_cast<size_t>(Value.Length()));
}

UnicodeString FromWide(const std::wstring & Value)
{
  return UnicodeString(Value.c_str(), static_cast<int32_t>(Value.size()));
}

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

bool FileExistsW(const std::wstring & Path)
{
  const DWORD Attributes = ::GetFileAttributesW(Path.c_str());
  return (Attributes != INVALID_FILE_ATTRIBUTES) && ((Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

std::wstring FormatErrorMessage(DWORD Error)
{
  wchar_t * Message = nullptr;
  const DWORD Length = ::FormatMessageW(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr, Error, 0, reinterpret_cast<wchar_t *>(&Message), 0, nullptr);
  std::wstring Result;
  if ((Length > 0) && (Message != nullptr))
  {
    Result.assign(Message, Length);
  }
  if (Message != nullptr)
  {
    ::LocalFree(Message);
  }
  Result = Trim(Result);
  if (Result.empty())
  {
    Result = L"Windows error " + std::to_wstring(Error);
  }
  return Result;
}

std::wstring QuoteArgument(const std::wstring & Value)
{
  bool NeedsQuotes = Value.empty();
  for (const wchar_t Ch : Value)
  {
    if ((Ch == L' ') || (Ch == L'\t') || (Ch == L'"'))
    {
      NeedsQuotes = true;
      break;
    }
  }
  if (!NeedsQuotes)
  {
    return Value;
  }

  std::wstring Result;
  Result.push_back(L'"');
  size_t Backslashes = 0;
  for (const wchar_t Ch : Value)
  {
    if (Ch == L'\\')
    {
      ++Backslashes;
    }
    else if (Ch == L'"')
    {
      Result.append(Backslashes * 2 + 1, L'\\');
      Result.push_back(L'"');
      Backslashes = 0;
    }
    else
    {
      Result.append(Backslashes, L'\\');
      Backslashes = 0;
      Result.push_back(Ch);
    }
  }
  Result.append(Backslashes * 2, L'\\');
  Result.push_back(L'"');
  return Result;
}

std::vector<wchar_t> BuildEnvironment(
  const std::vector<std::pair<std::wstring, std::wstring>> & Overrides)
{
  std::vector<std::wstring> Entries;
  wchar_t * Environment = ::GetEnvironmentStringsW();
  if (Environment != nullptr)
  {
    for (const wchar_t * Entry = Environment; *Entry != L'\0'; Entry += std::wcslen(Entry) + 1)
    {
      Entries.emplace_back(Entry);
    }
    ::FreeEnvironmentStringsW(Environment);
  }

  for (const std::pair<std::wstring, std::wstring> & Override : Overrides)
  {
    const std::wstring Prefix = Override.first + L"=";
    Entries.erase(
      std::remove_if(Entries.begin(), Entries.end(),
        [&Prefix](const std::wstring & Entry)
        {
          if (Entry.size() < Prefix.size())
          {
            return false;
          }
          for (size_t Index = 0; Index < Prefix.size(); ++Index)
          {
            if (std::towlower(Entry[Index]) != std::towlower(Prefix[Index]))
            {
              return false;
            }
          }
          return true;
        }),
      Entries.end());
    Entries.push_back(Prefix + Override.second);
  }

  std::vector<wchar_t> Result;
  for (const std::wstring & Entry : Entries)
  {
    Result.insert(Result.end(), Entry.begin(), Entry.end());
    Result.push_back(L'\0');
  }
  Result.push_back(L'\0');
  return Result;
}

void ReadPipe(HANDLE Pipe, std::vector<char> & Buffer, bool & Eof)
{
  if ((Pipe == nullptr) || Eof)
  {
    return;
  }

  for (int32_t Iteration = 0; Iteration < 4; ++Iteration)
  {
    DWORD Available = 0;
    if (!::PeekNamedPipe(Pipe, nullptr, 0, nullptr, &Available, nullptr))
    {
      const DWORD Error = ::GetLastError();
      if ((Error == ERROR_BROKEN_PIPE) || (Error == ERROR_PIPE_NOT_CONNECTED))
      {
        Eof = true;
      }
      return;
    }
    if (Available == 0)
    {
      return;
    }
    if (Available > ReadChunkLimit)
    {
      Available = ReadChunkLimit;
    }

    const size_t OldSize = Buffer.size();
    Buffer.resize(OldSize + Available);
    DWORD Read = 0;
    if (!::ReadFile(Pipe, Buffer.data() + OldSize, Available, &Read, nullptr))
    {
      Buffer.resize(OldSize);
      const DWORD Error = ::GetLastError();
      if ((Error == ERROR_BROKEN_PIPE) || (Error == ERROR_PIPE_NOT_CONNECTED))
      {
        Eof = true;
      }
      return;
    }
    Buffer.resize(OldSize + Read);
    if (Read == 0)
    {
      return;
    }
  }
}

std::wstring GetPluginDirectory()
{
  HMODULE Module = ::GetModuleHandleW(L"SshBox.dll");
  if (Module == nullptr)
  {
    Module = ::GetModuleHandleW(L"NetBox.dll");
  }
  if (Module == nullptr)
  {
    return std::wstring();
  }
  wchar_t Buffer[32768];
  const DWORD Length = ::GetModuleFileNameW(Module, Buffer, static_cast<DWORD>(_countof(Buffer)));
  if ((Length == 0) || (Length >= _countof(Buffer)))
  {
    return std::wstring();
  }
  std::wstring Result(Buffer, Length);
  const size_t Position = Result.find_last_of(L"\\/");
  if (Position == std::wstring::npos)
  {
    return std::wstring();
  }
  return Result.substr(0, Position);
}
} // namespace

TProcess::TProcess() noexcept = default;

TProcess::~TProcess() noexcept
{
  Close();
}

bool TProcess::Start(const std::vector<UnicodeString> & Arguments,
  const UnicodeString & Password, const UnicodeString & Passphrase,
  const UnicodeString & SshPath, const UnicodeString & AskpassPipe,
  uint64_t AskpassToken, UnicodeString & Error)
{
  Close();

  const UnicodeString SshExecutable = FindSshExecutable(SshPath);
  if (SshExecutable.IsEmpty())
  {
    Error = L"OpenSSH client (ssh.exe) not found. Install OpenSSH Client, set the path in Integration settings, or set SSHBOX_SSH_EXE.";
    return false;
  }

  std::wstring CommandLine = QuoteArgument(ToWide(SshExecutable));
  for (const UnicodeString & Argument : Arguments)
  {
    CommandLine.push_back(L' ');
    CommandLine += QuoteArgument(ToWide(Argument));
  }

  std::vector<std::pair<std::wstring, std::wstring>> EnvironmentOverrides;
  const UnicodeString Askpass = FindAskpassExecutable(SshPath);
  if (!Askpass.IsEmpty())
  {
    EnvironmentOverrides.emplace_back(L"SSH_ASKPASS", ToWide(Askpass));
    EnvironmentOverrides.emplace_back(L"SSH_ASKPASS_REQUIRE", L"force");
    // Older OpenSSH releases only use SSH_ASKPASS when DISPLAY is set.
    EnvironmentOverrides.emplace_back(L"DISPLAY", L"SshBox");
  }
  else
  {
    // Do not let an inherited helper interfere when the plugin has none.
    EnvironmentOverrides.emplace_back(L"SSH_ASKPASS", L"");
    EnvironmentOverrides.emplace_back(L"SSH_ASKPASS_REQUIRE", L"never");
  }
  // Always override: an inherited value must never authenticate a session.
  EnvironmentOverrides.emplace_back(L"NETBOX_ASKPASS_PASSWORD", ToWide(Password));
  EnvironmentOverrides.emplace_back(L"NETBOX_ASKPASS_PASSPHRASE", ToWide(Passphrase));
  EnvironmentOverrides.emplace_back(L"NETBOX_ASKPASS_PIPE", ToWide(AskpassPipe));
  EnvironmentOverrides.emplace_back(L"NETBOX_ASKPASS_TOKEN", std::to_wstring(AskpassToken));
  const std::vector<wchar_t> Environment = BuildEnvironment(EnvironmentOverrides);

  SECURITY_ATTRIBUTES SecurityAttributes{};
  SecurityAttributes.nLength = sizeof(SecurityAttributes);
  SecurityAttributes.bInheritHandle = TRUE;

  HANDLE StdinRead = nullptr;
  HANDLE StdinWrite = nullptr;
  HANDLE StdoutRead = nullptr;
  HANDLE StdoutWrite = nullptr;
  HANDLE StderrRead = nullptr;
  HANDLE StderrWrite = nullptr;

  bool Created = true;
  Created = Created && ::CreatePipe(&StdinRead, &StdinWrite, &SecurityAttributes, PipeBufferSize);
  Created = Created && ::CreatePipe(&StdoutRead, &StdoutWrite, &SecurityAttributes, PipeBufferSize);
  Created = Created && ::CreatePipe(&StderrRead, &StderrWrite, &SecurityAttributes, PipeBufferSize);
  if (!Created)
  {
    Error = FromWide(FormatErrorMessage(::GetLastError()));
  }

  if (Created)
  {
    ::SetHandleInformation(StdinWrite, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(StdoutRead, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(StderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW StartupInfo{};
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.hStdInput = StdinRead;
    StartupInfo.hStdOutput = StdoutWrite;
    StartupInfo.hStdError = StderrWrite;

    PROCESS_INFORMATION ProcessInfo{};
    std::vector<wchar_t> MutableCommandLine(CommandLine.begin(), CommandLine.end());
    MutableCommandLine.push_back(L'\0');

    const BOOL Result = ::CreateProcessW(
      SshExecutable.c_str(), MutableCommandLine.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
      const_cast<wchar_t *>(Environment.data()), nullptr, &StartupInfo, &ProcessInfo);
    if (!Result)
    {
      Error = FromWide(FormatErrorMessage(::GetLastError()));
      Created = false;
    }
    else
    {
      FProcess = ProcessInfo.hProcess;
      FProcessThread = ProcessInfo.hThread;
      FProcessId = ProcessInfo.dwProcessId;
    }
  }

  if (StdinRead != nullptr)
  {
    ::CloseHandle(StdinRead);
  }
  if (StdoutWrite != nullptr)
  {
    ::CloseHandle(StdoutWrite);
  }
  if (StderrWrite != nullptr)
  {
    ::CloseHandle(StderrWrite);
  }

  if (!Created)
  {
    if (StdinWrite != nullptr)
    {
      ::CloseHandle(StdinWrite);
    }
    if (StdoutRead != nullptr)
    {
      ::CloseHandle(StdoutRead);
    }
    if (StderrRead != nullptr)
    {
      ::CloseHandle(StderrRead);
    }
    return false;
  }

  FStdin = StdinWrite;
  FStdout = StdoutRead;
  FStderr = StderrRead;
  FStdoutEof = false;
  FStderrEof = false;
  FExited = false;
  FExitCode = 0;
  return true;
}

void TProcess::Close()
{
  if (FStdin != nullptr)
  {
    ::CloseHandle(static_cast<HANDLE>(FStdin));
    FStdin = nullptr;
  }

  if (FProcess != nullptr)
  {
    if (::WaitForSingleObject(static_cast<HANDLE>(FProcess), 1000) == WAIT_TIMEOUT)
    {
      ::TerminateProcess(static_cast<HANDLE>(FProcess), 1);
      ::WaitForSingleObject(static_cast<HANDLE>(FProcess), 1000);
    }
  }

  CloseHandles();
  FExited = false;
  FExitCode = 0;
  FStdoutEof = false;
  FStderrEof = false;
}

void TProcess::CloseHandles()
{
  if (FStdout != nullptr)
  {
    ::CloseHandle(static_cast<HANDLE>(FStdout));
    FStdout = nullptr;
  }
  if (FStderr != nullptr)
  {
    ::CloseHandle(static_cast<HANDLE>(FStderr));
    FStderr = nullptr;
  }
  if (FProcessThread != nullptr)
  {
    ::CloseHandle(static_cast<HANDLE>(FProcessThread));
    FProcessThread = nullptr;
  }
  if (FProcess != nullptr)
  {
    ::CloseHandle(static_cast<HANDLE>(FProcess));
    FProcess = nullptr;
  }
  FProcessId = 0;
}

bool TProcess::Write(const uint8_t * Data, size_t Length, UnicodeString & Error)
{
  if (FStdin == nullptr)
  {
    Error = L"OpenSSH process is not running.";
    return false;
  }

  size_t Offset = 0;
  while (Offset < Length)
  {
    const DWORD Chunk = static_cast<DWORD>(
      std::min<size_t>(Length - Offset, static_cast<size_t>(0x7FFFFFFF)));
    DWORD Written = 0;
    if (!::WriteFile(static_cast<HANDLE>(FStdin), Data + Offset, Chunk, &Written, nullptr))
    {
      Error = FromWide(FormatErrorMessage(::GetLastError()));
      return false;
    }
    if (Written == 0)
    {
      Error = L"OpenSSH process closed its input.";
      return false;
    }
    Offset += Written;
  }
  return true;
}

void TProcess::Pump(std::vector<uint8_t> & Stdout, std::string & Stderr)
{
  std::vector<char> Buffer;
  ReadPipe(static_cast<HANDLE>(FStdout), Buffer, FStdoutEof);
  const size_t OldSize = Stdout.size();
  Stdout.resize(OldSize + Buffer.size());
  for (size_t Index = 0; Index < Buffer.size(); ++Index)
  {
    Stdout[OldSize + Index] = static_cast<uint8_t>(Buffer[Index]);
  }

  Buffer.clear();
  ReadPipe(static_cast<HANDLE>(FStderr), Buffer, FStderrEof);
  Stderr.append(Buffer.begin(), Buffer.end());

  UpdateExitCode();
}

void TProcess::UpdateExitCode()
{
  if ((FProcess == nullptr) || FExited)
  {
    return;
  }
  if (::WaitForSingleObject(static_cast<HANDLE>(FProcess), 0) == WAIT_OBJECT_0)
  {
    DWORD ExitCode = 0;
    if (::GetExitCodeProcess(static_cast<HANDLE>(FProcess), &ExitCode))
    {
      FExitCode = ExitCode;
    }
    FExited = true;
  }
}

bool TProcess::IsRunning()
{
  UpdateExitCode();
  return (FProcess != nullptr) && !FExited;
}

bool TProcess::HasExited()
{
  UpdateExitCode();
  return (FProcess == nullptr) || FExited;
}

bool TProcess::HasOutput() const
{
  if (FStdout == nullptr)
  {
    return false;
  }
  DWORD Available = 0;
  return (::PeekNamedPipe(static_cast<HANDLE>(FStdout), nullptr, 0, nullptr, &Available, nullptr) != FALSE) &&
    (Available > 0);
}

bool TProcess::GetExitCode(uint32_t & ExitCode)
{
  UpdateExitCode();
  if ((FProcess == nullptr) || !FExited)
  {
    return false;
  }
  ExitCode = FExitCode;
  return true;
}

bool TProcess::IsAvailable(const UnicodeString & SshPath)
{
  return !FindSshExecutable(SshPath).IsEmpty();
}

UnicodeString TProcess::FindSshExecutable(const UnicodeString & SshPath)
{
  wchar_t Buffer[32768];

  if (!SshPath.IsEmpty())
  {
    std::wstring Candidate = ToWide(SshPath);
    if (FileExistsW(Candidate))
    {
      return FromWide(Candidate);
    }
    const DWORD Attributes = ::GetFileAttributesW(Candidate.c_str());
    if ((Attributes != INVALID_FILE_ATTRIBUTES) && ((Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
    {
      Candidate += L"\\ssh.exe";
      if (FileExistsW(Candidate))
      {
        return FromWide(Candidate);
      }
    }
  }

  const wchar_t * const EnvironmentNames[] = { L"SSHBOX_SSH_EXE", L"NETBOX_SSH_EXE" };
  for (const wchar_t * Name : EnvironmentNames)
  {
    const DWORD Length = ::GetEnvironmentVariableW(Name, Buffer, static_cast<DWORD>(_countof(Buffer)));
    if ((Length > 0) && (Length < _countof(Buffer)))
    {
      const std::wstring Override(Buffer, Length);
      if (FileExistsW(Override))
      {
        return FromWide(Override);
      }
    }
  }

  const DWORD WindowsLength = ::GetWindowsDirectoryW(Buffer, static_cast<DWORD>(_countof(Buffer)));
  if ((WindowsLength > 0) && (WindowsLength < _countof(Buffer)))
  {
    const std::wstring Candidate = std::wstring(Buffer, WindowsLength) + L"\\System32\\OpenSSH\\ssh.exe";
    if (FileExistsW(Candidate))
    {
      return FromWide(Candidate);
    }
  }

  const DWORD Length = ::SearchPathW(nullptr, L"ssh.exe", nullptr, static_cast<DWORD>(_countof(Buffer)), Buffer, nullptr);
  if ((Length > 0) && (Length < _countof(Buffer)))
  {
    const std::wstring Candidate(Buffer, Length);
    if (FileExistsW(Candidate))
    {
      return FromWide(Candidate);
    }
  }

  return UnicodeString();
}

UnicodeString TProcess::FindAskpassExecutable(const UnicodeString & SshPath)
{
  const wchar_t * const Names[] = { L"SshBoxAskpass.exe", L"NetBoxAskpass.exe" };

  const std::wstring Directory = GetPluginDirectory();
  if (!Directory.empty())
  {
    for (const wchar_t * Name : Names)
    {
      const std::wstring Candidate = Directory + L"\\" + Name;
      if (FileExistsW(Candidate))
      {
        return FromWide(Candidate);
      }
    }
  }

  const UnicodeString SshExecutable = FindSshExecutable(SshPath);
  if (!SshExecutable.IsEmpty())
  {
    std::wstring SshDirectory = ToWide(SshExecutable);
    const size_t Position = SshDirectory.find_last_of(L"\\/");
    if (Position != std::wstring::npos)
    {
      SshDirectory.erase(Position);
      for (const wchar_t * Name : Names)
      {
        const std::wstring Candidate = SshDirectory + L"\\" + Name;
        if (FileExistsW(Candidate))
        {
          return FromWide(Candidate);
        }
      }
    }
  }

  wchar_t Buffer[32768];
  for (const wchar_t * Name : Names)
  {
    const DWORD Length = ::SearchPathW(nullptr, Name, nullptr,
      static_cast<DWORD>(_countof(Buffer)), Buffer, nullptr);
    if ((Length > 0) && (Length < _countof(Buffer)))
    {
      const std::wstring Candidate(Buffer, Length);
      if (FileExistsW(Candidate))
      {
        return FromWide(Candidate);
      }
    }
  }

  return UnicodeString();
}
} // namespace OpenSsh
