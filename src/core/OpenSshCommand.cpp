
#include <vcl.h>
#pragma hdrstop

#include <Common.h>
#include <Sysutils.hpp>
#include <StrUtils.hpp>
#include <UnicodeString.hpp>

#include "OpenSshCommand.h"
#include "OpenSshConfig.h"
#include "OpenSshProcess.h"
#include "SessionData.h"

#include <string>
#include <vector>

namespace OpenSsh
{
namespace
{
UnicodeString ExpandKeyPath(const UnicodeString & Path)
{
  UnicodeString Result = ::ExpandEnvironmentVariables(Path);
  if ((Result.Length() >= 3) && (Result[1] == L'~') &&
    ((Result[2] == L'\\') || (Result[2] == L'/')))
  {
    const UnicodeString Home = TConfigReader::GetDefaultIncludeBaseDir();
    const int32_t Parent = Home.RPos(L'\\');
    if (Parent > 1)
    {
      Result = Home.SubString(1, Parent - 1) + Result.SubString(2, Result.Length() - 1);
    }
  }
  return Result;
}

void AddOption(std::vector<UnicodeString> & Arguments, const wchar_t * Name, const UnicodeString & Value)
{
  if (!Name || (*Name == L'\0') || Value.IsEmpty())
  {
    return;
  }
  Arguments.push_back(L"-o");
  Arguments.push_back(UnicodeString(Name) + L"=" + Value);
}

void AddOption(std::vector<UnicodeString> & Arguments, const wchar_t * Name, int32_t Value)
{
  if (Value == 0)
  {
    return;
  }
  Arguments.push_back(L"-o");
  Arguments.push_back(UnicodeString(Name) + L"=" + UnicodeString(std::to_wstring(Value).c_str()));
}

UnicodeString JumpHost(const TSessionData * Data)
{
  UnicodeString Host = Data->GetTunnelHostName();
  const int32_t Port = Data->GetTunnelPortNumber();
  if (Host.Pos(L":") > 0)
  {
    Host = L"[" + Host + L"]";
  }
  if (Port > 0)
  {
    Host += L":" + UnicodeString(std::to_wstring(Port).c_str());
  }
  if (!Data->GetTunnelUserName().IsEmpty())
  {
    Host = Data->GetTunnelUserName() + L"@" + Host;
  }
  return Host;
}
} // namespace

bool CanUseOpenSsh(const TSessionData * Data)
{
  if (Data == nullptr)
  {
    return false;
  }

  wchar_t Override[64]{};
  const DWORD OverrideLength = ::GetEnvironmentVariableW(
    L"NETBOX_SSH_CLIENT", Override, static_cast<DWORD>(_countof(Override)));
  if (OverrideLength > 0 && OverrideLength < _countof(Override))
  {
    if (::lstrcmpiW(Override, L"putty") == 0)
    {
      return false;
    }
    // Any other value (including "openssh") keeps the automatic selection.
  }

  if (!Data->GetUsesSsh() || !TProcess::IsAvailable())
  {
    return false;
  }

  switch (Data->GetFSProtocol())
  {
    case fsSFTP:
    case fsSFTPonly:
    case fsSCPonly:
      break;
    default:
      return false;
  }

  if (Data->GetInteractiveTerminal() || Data->GetFingerprintScan() || Data->GetSshNoUserAuth())
  {
    return false;
  }

  if (Data->GetProxyMethod() != pmNone && Data->GetProxyMethod() != pmCmd)
  {
    return false;
  }

  if (Data->GetTunnel() && Data->GetTunnelHostName().IsEmpty())
  {
    return false;
  }

  return true;
}

std::vector<UnicodeString> BuildSshArguments(const TSessionData * Data, bool Scp)
{
  std::vector<UnicodeString> Arguments;

  // Never request a pseudo terminal: the shell/SCP logic of NetBox expects a
  // plain byte stream, matching PuTTY's "nopty" mode.
  Arguments.push_back(L"-T");

  const bool Askpass = !TProcess::FindAskpassExecutable().IsEmpty();
  AddOption(Arguments, L"BatchMode", Askpass ? L"no" : L"yes");
  AddOption(Arguments, L"LogLevel", L"ERROR");
  if (Askpass)
  {
    AddOption(Arguments, L"NumberOfPasswordPrompts", 1);
  }

  const int32_t Timeout = Data->GetTimeout();
  if (Timeout > 0)
  {
    AddOption(Arguments, L"ConnectTimeout", Timeout);
  }
  if (Data->GetPingInterval() > 0)
  {
    AddOption(Arguments, L"ServerAliveInterval", Data->GetPingInterval());
    AddOption(Arguments, L"ServerAliveCountMax", 3);
  }
  if (Data->GetCompression())
  {
    AddOption(Arguments, L"Compression", L"yes");
  }

  switch (Data->GetAddressFamily())
  {
    case afIPv4:
      Arguments.push_back(L"-4");
      break;
    case afIPv6:
      Arguments.push_back(L"-6");
      break;
    default:
      break;
  }

  const UnicodeString SourceAddress = Data->SourceAddress;
  if (!SourceAddress.IsEmpty())
  {
    Arguments.push_back(L"-b");
    Arguments.push_back(SourceAddress);
  }

  if (Data->GetPortNumber() != SshPortNumber)
  {
    Arguments.push_back(L"-p");
    Arguments.push_back(UnicodeString(std::to_wstring(Data->GetPortNumber()).c_str()));
  }
  if (!Data->GetUserName().IsEmpty())
  {
    Arguments.push_back(L"-l");
    Arguments.push_back(Data->GetUserName());
  }
  if (!Data->GetPublicKeyFile().IsEmpty())
  {
    Arguments.push_back(L"-i");
    Arguments.push_back(ExpandKeyPath(Data->GetPublicKeyFile()));
    // Respect exactly the key selected by the user.
    AddOption(Arguments, L"IdentitiesOnly", L"yes");
  }

  if (Data->GetProxyMethod() == pmCmd && !Data->GetProxyLocalCommand().IsEmpty())
  {
    AddOption(Arguments, L"ProxyCommand", Data->GetProxyLocalCommand());
  }

  // ProxyJump from NetBox tunnel settings. Configuration file ProxyJump is
  // resolved by ssh itself.
  if (Data->GetTunnel())
  {
    Arguments.push_back(L"-J");
    Arguments.push_back(JumpHost(Data));
  }

  if (!Scp)
  {
    Arguments.push_back(L"-s");
  }

  // End of options; the destination is a configuration alias and may contain
  // characters that look like options.
  Arguments.push_back(L"--");
  Arguments.push_back(Data->GetHostNameExpanded());

  if (!Scp)
  {
    Arguments.push_back(L"sftp");
  }

  return Arguments;
}
} // namespace OpenSsh
