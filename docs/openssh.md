# Native OpenSSH backend

Far-NetBox can use the OpenSSH client that ships with Windows instead of the
built-in PuTTY engine for SSH sessions. This unlocks the full
`~/.ssh/config` feature set: key authentication, ssh-agent, `ProxyJump`,
`Include`, `Match`, per-host ciphers and everything else OpenSSH understands.

FTP/FTPS, WebDAV and S3 continue to use their existing implementations.

## Requirements

| Component | Requirement |
|-----------|-------------|
| Plugin | `NetBox.dll` + `NetBoxAskpass.exe` in the same plugin directory |
| SSH client | `ssh.exe` from Windows OpenSSH (Windows 10 1809+), or `NETBOX_SSH_EXE` |
| Configuration | `%HOME%\.ssh\config` or `%USERPROFILE%\.ssh\config` |

If `ssh.exe` is not found, the plugin silently keeps using the PuTTY backend.

## How sessions are selected

The OpenSSH backend is used automatically for SFTP and SCP sessions when:

* `ssh.exe` is available;
* the session does not request an interactive terminal, fingerprint scan,
  password-change-only mode or a SOCKS/HTTP proxy;
* the session does not use a local proxy command that SSH cannot express.

`NETBOX_SSH_CLIENT=putty` forces the PuTTY backend for troubleshooting.
`NETBOX_SSH_EXE=C:\path\to\ssh.exe` overrides the client location.

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
* Hosts from the configuration are read-only in NetBox. Use `F4` on them to see
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
  needed in NetBox.
* **Passwords and key passphrases** — OpenSSH calls `NetBoxAskpass.exe`. If the
  NetBox session has a stored password, it is passed to the helper through a
  private environment variable. A stored key passphrase is passed separately
  and used when OpenSSH asks for a passphrase. Otherwise the helper shows a
  small native dialog.
* **PuTTY keys** — OpenSSH cannot read `.ppk` files. A session that specifies a
  `.ppk` key automatically keeps using the PuTTY backend.
* **Host keys** — verification is performed by OpenSSH against the standard
  `known_hosts` file. When OpenSSH asks whether to trust a new key, the helper
  shows a Yes/No dialog.

Passwords are written to the helper as UTF-8. ASCII passwords are unaffected;
for non-ASCII passwords prefer key authentication.

## ProxyJump

`ProxyJump` in the configuration is executed by OpenSSH, including jump chains.
NetBox's own tunnel settings (`Tunnel`/`TunnelHostName`) are translated into a
single `-J user@host:port` argument; the internal WinSCP-style tunnel is skipped.

SOCKS4/SOCKS5/HTTP proxies are not supported by `ssh.exe` and make the session
fall back to PuTTY. `ProxyCommand` (NetBox "Cmd" proxy method) is passed
through as `-o ProxyCommand=...`.

## Implementation notes

| File | Purpose |
|------|---------|
| `src/core/OpenSshConfig.{h,cpp}` | Parses `~/.ssh/config`, follows `Include`, builds the host tree |
| `src/core/OpenSshProcess.{h,cpp}` | Non-blocking `ssh.exe` process with stdin/stdout/stderr pipes |
| `src/core/OpenSshCommand.{h,cpp}` | Decides when native SSH is usable and builds the argument list |
| `src/core/SecureShell.cpp` | Routes the existing SFTP/SCP engines through the OpenSSH transport |
| `src/NetBoxAskpass/` | `SSH_ASKPASS` helper |
| `src/NetBox/WinSCPFileSystem.cpp` | Shows configuration hosts in the session panel |

The existing `TSFTPFileSystem` and `TSCPFileSystem` are intentionally reused.
Only the byte stream underneath `TSecureShell` changes: `ssh -s host sftp`
provides the SFTP channel, and a plain `ssh -T host` shell provides the SCP
channel. This keeps queueing, progress, symlinks, properties and file transfer
semantics identical to the PuTTY-based implementation.

## Limitations

* Windows 10/11 is required for the OpenSSH backend (the plugin itself still
  builds for x86/x64/ARM64, and the other protocols keep their previous
  platform support).
* Interactive terminal sessions, fingerprint scanning and password changes are
  not implemented on this path and fall back to PuTTY.
* `SCP` runs through an OpenSSH shell. Servers that disable the shell but keep
  the SFTP subsystem should be used with the SFTP protocol.
* The session list is refreshed when the plugin session panel is reopened.

## Troubleshooting

| Symptom | Check |
|---------|-------|
| No hosts from the config | `~/.ssh/config` exists; `Include` paths resolve; `ssh -G host` works in a terminal |
| "ssh.exe not found" in the log | Install **OpenSSH Client** in Windows optional features, or set `NETBOX_SSH_EXE` |
| Host key prompt never appears | `NetBoxAskpass.exe` must be next to `NetBox.dll` |
| Authentication hangs | Close the askpass window and retry; check `known_hosts` permissions |
| Need the old behaviour | Set `NETBOX_SSH_CLIENT=putty` before starting Far Manager |
