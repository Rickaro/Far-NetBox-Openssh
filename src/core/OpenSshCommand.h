
#pragma once

#include <UnicodeString.hpp>

#include <vector>

class TSessionData;

namespace OpenSsh
{
// Returns true when the session can be served by the external OpenSSH client
// without losing functionality that only the PuTTY backend provides.
bool CanUseOpenSsh(const TSessionData * Data);

// Builds the ssh.exe argument list (excluding the executable itself).
// Scp selects an interactive shell channel (for the SCP protocol) instead of
// the sftp subsystem.
std::vector<UnicodeString> BuildSshArguments(const TSessionData * Data, bool Scp);
} // namespace OpenSsh
