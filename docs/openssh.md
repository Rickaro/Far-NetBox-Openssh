# Native OpenSSH backend

SshBox can use the OpenSSH client that ships with Windows instead of the
built-in PuTTY engine for SSH sessions. This unlocks the full
`~/.ssh/config` feature set: key authentication, ssh-agent, `ProxyJump`,
`Include`, `Match`, per-host ciphers and everything else OpenSSH understands.

FTP/FTPS, WebDAV and S3 continue to use their existing implementations.

## Requirements

| Component | Requirement |
|-----------|-------------|
| Plugin | `SshBox.dll` + `SshBoxAskpass.exe` in the same plugin directory |
| SSH client | `ssh.exe` from Windows OpenSSH (Windows 10 1809+), or `SSHBOX_SSH_EXE` |
| Configuration | `%HOME%\.ssh\config` or `%USERPROFILE%\.ssh\config` |

If `ssh.exe` is not found, the plugin silently keeps using the PuTTY backend.

## How sessions are selected

The OpenSSH backend is used automatically for SFTP and SCP sessions when:

* `ssh.exe` is available;
* the session does not request an interactive terminal, fingerprint scan,
  password-change-only mode or a SOCKS/HTTP proxy;
* the session does not use a local proxy command that SSH cannot express.

`SSHBOX_SSH_CLIENT=putty` forces the PuTTY backend for troubleshooting.
`SSHBOX_SSH_EXE=C:\path\to\ssh.exe` overrides the client location.

## Host list from `~/.ssh/config`

The stored-session panel is extended with every concrete `Host` alias found in
the user configuration:

* `Include` files become folders. Nested includes become nested folders.
* Glob includes (`Include config.d/*`) are expanded; every matched file becomes
  its own folder.
* Wildcard-only sections (`Host *`, `Host db-*`) are patterns, not connectable
  hosts, and are not shown.
* `Match` blocks never declare hosts and are ignored for the list.
* The first occurrence of an alias wins, matching OpenSSH's behaviour.
* Hosts from the configuration are read-only in SshBox. Use `F4` on them to see
  a reminder; edit `~/.ssh/config` instead.

Configuration entries are resolved by `ssh.exe` itself when you connect, so
`HostName`, `User`, `Port`, `IdentityFile`, `ProxyJump`, `Include`, `Match`,
`CanonicalizeHostname` and all other directives work exactly as they do on the
command line.

Relative `Include` paths are resolved against `~/.ssh`, as OpenSSH does. If the
`HOME` environment variable is set, it is used instead of `%USERPROFILE%`, also
matching OpenSSH for Windows.

## Authentication

* **Keys and ssh-agent** — handled directly by OpenSSH. No configuration is
  needed in SshBox.
* **Passwords and key passphrases** — OpenSSH calls `SshBoxAskpass.exe`. The
  helper asks the plugin through a per-session named pipe, and the plugin shows
  the standard Far password dialog. A stored password/passphrase is used
  without a dialog. If the plugin cannot be reached, the helper falls back to
  its own native window.
* **PuTTY keys** — OpenSSH cannot read `.ppk` files. A session that specifies a
  `.ppk` key automatically keeps using the PuTTY backend.
* **Host keys** — verification is performed by OpenSSH against the standard
  `known_hosts` file. When OpenSSH asks whether to trust a new key, the plugin
  shows the Far Yes/No dialog.

Passwords are written to the helper as UTF-8. ASCII passwords are unaffected;
for non-ASCII passwords prefer key authentication.

## ProxyJump

`ProxyJump` in the configuration is executed by OpenSSH, including jump chains.
SshBox's own tunnel settings (`Tunnel`/`TunnelHostName`) are translated into a
single `-J user@host:port` argument; the internal WinSCP-style tunnel is skipped.

SOCKS4/SOCKS5/HTTP proxies are not supported by `ssh.exe` and make the session
fall back to PuTTY. `ProxyCommand` (SshBox "Cmd" proxy method) is passed
through as `-o ProxyCommand=...`.

## Implementation notes

| File | Purpose |
|------|---------|
| `src/core/OpenSshConfig.{h,cpp}` | Parses `~/.ssh/config`, follows `Include`, builds the host tree |
| `src/core/OpenSshProcess.{h,cpp}` | Non-blocking `ssh.exe` process with stdin/stdout/stderr pipes |
| `src/core/OpenSshCommand.{h,cpp}` | Decides when native SSH is usable and builds the argument list |
| `src/core/SecureShell.cpp` | Routes the existing SFTP/SCP engines through the OpenSSH transport |
| `src/SshBoxAskpass/` | `SSH_ASKPASS` helper |
| `src/SshBox/WinSCPFileSystem.cpp` | Shows configuration hosts in the session panel |

The existing `TSFTPFileSystem` and `TSCPFileSystem` are intentionally reused.
Only the byte stream underneath `TSecureShell` changes: `ssh -s host sftp`
provides the SFTP channel, and a plain `ssh -T host` shell provides the SCP
channel. This keeps queueing, progress, symlinks, properties and file transfer
semantics identical to the PuTTY-based implementation.

## Limitations

* Windows 10/11 is required for the OpenSSH backend (the plugin itself still
  builds for x86/x64/ARM64, and the other protocols keep their previous
  platform support).
* Port-forwarding directives from `~/.ssh/config` (`LocalForward`,
  `RemoteForward`, `DynamicForward`) are not applied to SshBox sessions: the
  plugin passes `ClearAllForwardings=yes`, just like `sftp`/`scp` do.
* Interactive terminal sessions, fingerprint scanning and password changes are
  not implemented on this path and fall back to PuTTY.
* `SCP` runs through an OpenSSH shell. Servers that disable the shell but keep
  the SFTP subsystem should be used with the SFTP protocol.
* The session list is refreshed when the plugin session panel is reopened.

## Troubleshooting

| Symptom | Check |
|---------|-------|
| No hosts from the config | `~/.ssh/config` exists; `Include` paths resolve; `ssh -G host` works in a terminal |
| "ssh.exe not found" in the log | Install **OpenSSH Client** in Windows optional features, or set `SSHBOX_SSH_EXE` |
| Host key prompt never appears | `SshBoxAskpass.exe` must be next to `SshBox.dll` |
| Authentication hangs | Close the askpass window and retry; check `known_hosts` permissions |
| Key authentication asks for a password | See the diagnostic steps below |
| Need the old behaviour | Set `SSHBOX_SSH_CLIENT=putty` before starting Far Manager |

### Enabling the log

Logging is disabled by default, and the default file is **not**
`%LOCALAPPDATA%\SshBox\netbox.log`. It is:

```
%TEMP%\<session name>.log
```

Enable it in the plugin menu: open SshBox, press `Alt+Shift+W`, choose
**Logging settings**, tick **Enable logging** and **Log to file**. The dialog
shows the actual expanded path.

For OpenSSH-level diagnostics set `SSHBOX_SSH_VERBOSE=1` before starting Far.
The plugin then passes `-o LogLevel=DEBUG1` to `ssh`, and the debug lines land
in the session log.

### Key authentication asks for a password

First reproduce outside Far with the same environment:

```cmd
where ssh
ssh -V
echo %HOME%
echo %USERPROFILE%
ssh -G <alias> | findstr /i "identityfile user hostname port"
ssh -v <alias>
```

Typical causes:

* `IdentityFile` points to a `.ppk` file — OpenSSH cannot read it; convert it to
  OpenSSH format (`ssh-keygen -p -f key.ppk` in PuTTY tools or export from
  PuTTYgen).
* `HOME` is set to a different directory than `%USERPROFILE%`, so ssh looks for
  keys and `known_hosts` elsewhere.
* Windows OpenSSH rejects the private key because its ACL is too open. `ssh -v`
  prints `Permissions for ... are too open`. Fix the ACL so that only the
  current user has access, e.g. in PowerShell:
  ```powershell
  icacls "$env:USERPROFILE\.ssh\id_ed25519" /inheritance:r /grant:r "$env:USERNAME:(R)"
  ```
* The key is passphrase-protected and the passphrase is unknown or not in
  ssh-agent. The askpass dialog should say “passphrase”, not “password”.
* The server does not accept that key for the selected user. Compare the
  `Offering public key` / `Server accepts key` lines of `ssh -v`.

If ssh works in the console but the plugin still asks for a password, enable
`SSHBOX_SSH_VERBOSE=1` and compare the `Starting OpenSSH client:` line in the
session log with the working command.
