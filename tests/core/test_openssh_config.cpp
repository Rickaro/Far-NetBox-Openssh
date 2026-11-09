#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <Sysutils.hpp>
#include <OpenSshConfig.h>

#define CHECK(CONDITION) \
  do \
  { \
    if (!(CONDITION)) \
    { \
      std::cerr << "FAILED: " #CONDITION " at " << __FILE__ << ":" << __LINE__ << std::endl; \
      std::exit(1); \
    } \
  } while (false)
namespace
{
UnicodeString US(const std::wstring & Value)
{
  return UnicodeString(Value.c_str(), static_cast<int32_t>(Value.size()));
}

std::wstring TempDirectory()
{
  wchar_t Buffer[MAX_PATH]{};
  const DWORD Length = ::GetTempPathW(MAX_PATH, Buffer);
  CHECK(Length > 0);
  const std::wstring Base(Buffer, Length);
  static int32_t Counter = 0;
  const std::wstring Directory = Base + L"netbox-openssh-test-" +
    std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(++Counter);
  ::CreateDirectoryW(Directory.c_str(), nullptr);
  ::CreateDirectoryW((Directory + L"\\.ssh").c_str(), nullptr);
  return Directory;
}

void WriteFile(const std::wstring & Path, const std::string & Content)
{
  const HANDLE Handle = ::CreateFileW(Path.c_str(), GENERIC_WRITE, 0, nullptr,
    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(Handle != INVALID_HANDLE_VALUE);
  DWORD Written = 0;
  CHECK(::WriteFile(Handle, Content.data(), static_cast<DWORD>(Content.size()), &Written, nullptr) != FALSE);
  CHECK(Written == static_cast<DWORD>(Content.size()));
  ::CloseHandle(Handle);
}

std::wstring NameOf(const OpenSsh::THostEntry & Entry)
{
  return std::wstring(Entry.Host.c_str(), static_cast<size_t>(Entry.Host.Length()));
}

std::wstring FolderOf(const OpenSsh::THostEntry & Entry)
{
  return std::wstring(Entry.Folder.c_str(), static_cast<size_t>(Entry.Folder.Length()));
}

const OpenSsh::THostEntry * Find(
  const std::vector<OpenSsh::THostEntry> & Hosts, const std::wstring & Name)
{
  for (const OpenSsh::THostEntry & Entry : Hosts)
  {
    if (NameOf(Entry) == Name)
    {
      return &Entry;
    }
  }
  return nullptr;
}
} // namespace

int main()
{
  // --- Basic parsing: comments, "=" syntax, aliases, wildcard sections ---
  {
    const std::wstring Directory = TempDirectory();
    const std::wstring Ssh = Directory + L"\\.ssh";
    WriteFile(Ssh + L"\\config",
      "# comment\n"
      "\n"
      "Host web-prod\n"
      "  HostName 10.0.0.5\n"
      "  User deploy\n"
      "Host=db-1 db-2\n"
      "  User dba\n"
      "Host *\n"
      "  ServerAliveInterval 30\n"
      "Host db-*\n"
      "  Port 2222\n"
      "Match host web-prod\n"
      "  Include missing-file\n");

    OpenSsh::TConfigReader Reader(US(Ssh + L"\\config"), US(Ssh));
    Reader.Read();
    const std::vector<OpenSsh::THostEntry> & Hosts = Reader.GetHosts();
    CHECK(Hosts.size() == 3u);
    CHECK(Find(Hosts, L"web-prod") != nullptr);
    CHECK(Find(Hosts, L"db-1") != nullptr);
    CHECK(Find(Hosts, L"db-2") != nullptr);
    CHECK(Find(Hosts, L"*") == nullptr);
    CHECK(Find(Hosts, L"db-*") == nullptr);
    CHECK(Find(Hosts, L"web-prod")->Folder.IsEmpty());
    std::cout << "PASS: basic parsing" << std::endl;
  }

  // --- Include becomes a folder, nested includes become nested folders ---
  {
    const std::wstring Directory = TempDirectory();
    const std::wstring Ssh = Directory + L"\\.ssh";
    ::CreateDirectoryW((Ssh + L"\\config.d").c_str(), nullptr);
    ::CreateDirectoryW((Ssh + L"\\config.d\\nested").c_str(), nullptr);
    WriteFile(Ssh + L"\\config",
      "Host root-host\n"
      "Include config.d/work\n");
    WriteFile(Ssh + L"\\config.d\\work",
      "Host work-host\n"
      "Include nested/db\n");
    WriteFile(Ssh + L"\\config.d\\nested\\db",
      "Host db-host\n");

    OpenSsh::TConfigReader Reader(US(Ssh + L"\\config"), US(Ssh));
    Reader.Read();
    const std::vector<OpenSsh::THostEntry> & Hosts = Reader.GetHosts();
    CHECK(Hosts.size() == 3u);
    CHECK(FolderOf(*Find(Hosts, L"root-host")).empty());
    CHECK(FolderOf(*Find(Hosts, L"work-host")) == L"work");
    CHECK(FolderOf(*Find(Hosts, L"db-host")) == L"work/db");
    std::cout << "PASS: include folders" << std::endl;
  }

  // --- Glob includes and "~/" expansion ---
  {
    const std::wstring Directory = TempDirectory();
    const std::wstring Home = Directory + L"\\home";
    const std::wstring Ssh = Home + L"\\.ssh";
    ::CreateDirectoryW(Home.c_str(), nullptr);
    ::CreateDirectoryW(Ssh.c_str(), nullptr);
    ::CreateDirectoryW((Ssh + L"\\config.d").c_str(), nullptr);
    WriteFile(Ssh + L"\\config", "Include ~/.ssh/config.d/*\n");
    WriteFile(Ssh + L"\\config.d\\b.conf", "Host host-b\n");
    WriteFile(Ssh + L"\\config.d\\a.conf", "Host host-a\n");

    OpenSsh::TConfigReader Reader(US(Ssh + L"\\config"), US(Ssh));
    Reader.Read();
    const std::vector<OpenSsh::THostEntry> & Hosts = Reader.GetHosts();
    CHECK(Hosts.size() == 2u);
    CHECK(FolderOf(*Find(Hosts, L"host-a")) == L"a");
    CHECK(FolderOf(*Find(Hosts, L"host-b")) == L"b");
    std::cout << "PASS: glob include" << std::endl;
  }

  // --- Duplicate host keeps the first definition only ---
  {
    const std::wstring Directory = TempDirectory();
    const std::wstring Ssh = Directory + L"\\.ssh";
    WriteFile(Ssh + L"\\config",
      "Host same-host\n"
      "  User first\n"
      "Include more\n");
    WriteFile(Ssh + L"\\more",
      "Host SAME-HOST\n"
      "  User second\n"
      "Host other-host\n");

    OpenSsh::TConfigReader Reader(US(Ssh + L"\\config"), US(Ssh));
    Reader.Read();
    const std::vector<OpenSsh::THostEntry> & Hosts = Reader.GetHosts();
    CHECK(Hosts.size() == 2u);
    CHECK(Find(Hosts, L"same-host") != nullptr);
    CHECK(FolderOf(*Find(Hosts, L"same-host")).empty());
    CHECK(FolderOf(*Find(Hosts, L"other-host")) == L"more");
    std::cout << "PASS: duplicate hosts" << std::endl;
  }

  // --- Missing top-level config throws ---
  {
    const std::wstring Directory = TempDirectory();
    OpenSsh::TConfigReader Reader(
      US(Directory + L"\\.ssh\\absent"), US(Directory + L"\\.ssh"));
    bool Thrown = false;
    try
    {
      Reader.Read();
    }
    catch (const Exception &)
    {
      Thrown = true;
    }
    CHECK(Thrown);
    std::cout << "PASS: missing config" << std::endl;
  }

  std::cout << "All OpenSSH config tests passed." << std::endl;
  return 0;
}
