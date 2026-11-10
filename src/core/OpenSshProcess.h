
#pragma once

#include <UnicodeString.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace OpenSsh
{
// Owns a child OpenSSH client process (ssh.exe) connected through anonymous
// pipes. All pipe I/O is non-blocking on the reading side so that the caller
// can keep the Far Manager UI responsive and process SFTP/SCP data on its own
// thread.
class TProcess
{
  TProcess(const TProcess &) = delete;
  TProcess & operator=(const TProcess &) = delete;

public:
  TProcess() noexcept;
  ~TProcess() noexcept;

  bool Start(const std::vector<UnicodeString> & Arguments,
    const UnicodeString & Password, const UnicodeString & Passphrase,
    const UnicodeString & SshPath, const UnicodeString & SshAgentPath,
    const UnicodeString & AskpassPipe, uint64_t AskpassToken, UnicodeString & Error);
  void Close();

  bool Write(const uint8_t * Data, size_t Length, UnicodeString & Error);

  // Appends everything currently available from stdout/stderr. Never blocks.
  void Pump(std::vector<uint8_t> & Stdout, std::string & Stderr);

  bool IsRunning();
  bool IsStarted() const { return FProcess != nullptr; }
  bool HasOutput() const;
  bool HasExited();  bool GetExitCode(uint32_t & ExitCode);

  static bool IsAvailable(const UnicodeString & SshPath = UnicodeString());
  static UnicodeString FindSshExecutable(const UnicodeString & SshPath = UnicodeString());
  static UnicodeString FindAskpassExecutable(const UnicodeString & SshPath = UnicodeString());
  static UnicodeString FindSshAgentExecutable(const UnicodeString & SshPath = UnicodeString(),
    const UnicodeString & SshAgentPath = UnicodeString());

  // True when an ssh-agent is already reachable (SSH_AUTH_SOCK or the Windows
  // agent pipe) or was started by the plugin during this Far session.
  static bool HasSshAgent();
  static UnicodeString SshAgentSocket();
  // Starts ssh-agent from the configured path when no agent is reachable.
  // Never blocks for long and never fails the session: when no agent can be
  // started, ssh simply prompts for the key passphrase through askpass.
  static bool EnsureSshAgent(const UnicodeString & SshPath, const UnicodeString & SshAgentPath,
    UnicodeString & Error);
  // Terminates the agent started by the plugin, if any.
  static void ShutdownSshAgent();

private:
  void CloseHandles();
  void UpdateExitCode();

  void * FProcess{nullptr};
  void * FProcessThread{nullptr};
  uint32_t FProcessId{0};
  void * FStdin{nullptr};
  void * FStdout{nullptr};
  void * FStderr{nullptr};
  bool FStdoutEof{false};
  bool FStderrEof{false};
  bool FExited{false};
  uint32_t FExitCode{0};
};
} // namespace OpenSsh
