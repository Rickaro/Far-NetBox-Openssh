
#pragma once

#include <UnicodeString.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace OpenSsh
{
// A concrete host alias declared by a "Host" directive in an OpenSSH
// configuration file. Wildcard-only sections (for example "Host *") are not
// hosts and are intentionally not returned.
struct THostEntry
{
  // Alias exactly as it appears in the configuration file. It is passed to
  // ssh.exe as the destination, so ssh resolves HostName/User/Port/ProxyJump
  // and every other directive itself.
  UnicodeString Host;
  // Folder in the NetBox session list derived from the Include file that
  // declared the host. Empty for the top-level configuration file. Components
  // are separated with '/'.
  UnicodeString Folder;
  // Absolute path of the file that declared the host.
  UnicodeString SourceFile;
  // 1-based line number of the Host directive.
  int32_t Line{0};
};

// Reads the user OpenSSH configuration and builds a flat list of connectable
// host aliases. Include directives are followed recursively (with glob
// support), each included file becoming a folder in the resulting list.
class TConfigReader
{
public:
  TConfigReader(const UnicodeString & ConfigPath, const UnicodeString & IncludeBaseDir);

  // Throws Exception when the top-level configuration file cannot be read.
  // Missing included files are reported through GetWarnings() instead, like
  // OpenSSH does.
  void Read();

  const std::vector<THostEntry> & GetHosts() const { return FHosts; }
  const std::vector<UnicodeString> & GetWarnings() const { return FWarnings; }

  static UnicodeString GetDefaultConfigPath();
  static UnicodeString GetDefaultIncludeBaseDir();

private:
  void ReadFile(const std::wstring & Path, const std::wstring & Folder);

  UnicodeString FConfigPath;
  UnicodeString FIncludeBaseDir;
  std::vector<THostEntry> FHosts;
  std::vector<UnicodeString> FWarnings;
  std::vector<std::wstring> FStack;
};
} // namespace OpenSsh
