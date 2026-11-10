# SshBox — Far-NetBox OpenSSH fork

> SFTP/SCP client plugin for Far Manager 3.0 using the system OpenSSH client

**Plugin version:** 0.1.0
**Based on:** Far-NetBox **26.7.3.617** ([michaellukashov/Far-NetBox](https://github.com/michaellukashov/Far-NetBox))

SshBox is an AI-assisted fork of Far-NetBox focused on native OpenSSH. It uses
`ssh.exe` and `ssh-agent` as the transport instead of the bundled PuTTY engine,
reads `~/.ssh/config` (including `Include`, `Match` and `ProxyJump`), and can be
installed next to the original NetBox plugin.

The plugin is built as `SshBox.dll` and installed into `Plugins\SshBox`, so it
does not replace or conflict with `Plugins\NetBox`.

## Features

- **Flexible SSH client support** — the built-in Windows OpenSSH client or a
  custom `ssh.exe` (for example the one shipped with Git for Windows).
- **Robust SSH config support** — `~/.ssh/config`, `ProxyJump`, key-based
  authentication and partial configurations through `Include`.
- **Jump host connectivity** — connect any host through a jump host with
  `Ctrl+Shift+J`, without adding `host-via-jump` entries to the configuration.
- **Coexistence** — runs alongside the original Far-NetBox plugin without
  conflicts (own DLL, plugin directory, command prefix and configuration key).
- **Far-native prompts** — password, key passphrase and host-key prompts are
  shown by Far itself through a per-session named-pipe bridge.
- **PuTTY fallback** — PuTTY remains available for `.ppk` keys, SOCKS/HTTP
  proxies and interactive terminal sessions.
- **Multi-architecture** — x86, x64 and ARM64 builds.

## Disclaimer

**NOTE:** This is an AI-generated codebase. Please review the code and use it at
your own risk.

## Quick Start

```cmd
:: Build for x64 (requires Visual Studio 2022)
cmd /c build-x64.bat

:: Launch Far Manager from the platform directory
Far3_x64\Far.exe

:: Press F11 and select SshBox
```

## OpenSSH settings

### Integration settings (recommended)

Open the plugin commands menu with **`Alt+Shift+W`**, then choose
**Integration settings**. The dialog contains:

- **SSH client (mode)** — `Auto`, `OpenSSH` or `PuTTY`. `Auto` uses OpenSSH
  when `ssh.exe` is available and falls back to PuTTY; `OpenSSH` always uses
  the external client; `PuTTY` never does.
- **OpenSSH client (`ssh.exe`)** — full path to the `ssh.exe` to use. Empty
  means automatic search; the dialog shows the standard Windows path when it
  exists.
- **OpenSSH agent (`ssh-agent.exe`)** — path to the agent used for key
  authentication. See [SSH agent](#ssh-agent) below.
- Putty/Pageant/PuTTYgen paths — used only by the PuTTY backend.

All paths are stored in the SshBox configuration and take precedence over
environment variables. The client path must point to `ssh.exe` (not
`scp.exe`); a directory containing `ssh.exe` is also accepted.

### Connect through a jump host

Select a host in the session list and press **`Ctrl+Shift+J`** (or use
**`Alt+Shift+W` → Connect via jump host**). A dialog asks for the jump host:

- pick an alias from `~/.ssh/config` in the drop-down, or type
  `user@host[:port]` (IPv6 literals in brackets are accepted);
- the session is connected with `-J <jump>` and the panel title becomes
  `<session> via <jump>`;
- nothing is written to the SshBox configuration and no `host-via-jump` entry
  is added to `~/.ssh/config`;
- the value is remembered for the rest of the Far session, so the next
  `Ctrl+Shift+J` starts from it.

The candidate list contains every concrete host alias from `~/.ssh/config`
(including `Include` files) plus the hosts of stored SSH sessions. Because ssh
resolves the alias itself, its `User`, `Port`, `IdentityFile` and `ProxyJump`
apply to the jump connection. In PuTTY mode the same settings drive the
built-in tunnel instead of `-J`.

### SSH agent

The plugin picks the first agent that is available:

1. an inherited `SSH_AUTH_SOCK` in the Far environment;
2. the running Windows agent pipe `\\.\pipe\openssh-ssh-agent`;
3. an agent started by the plugin from the configured `ssh-agent.exe`.

When no agent is running, the plugin starts the configured binary, reads
`SSH_AUTH_SOCK`/`SSH_AGENT_PID` from its output and passes them to every `ssh`
it launches. ssh is then started with `-o AddKeysToAgent=yes`, so a key is
decrypted once and stays in the agent for the rest of the Far session — later
connections do not ask for the passphrase again. The agent started by the
plugin is stopped when Far exits.

Windows' own `C:\Windows\System32\OpenSSH\ssh-agent.exe` is a **service**
binary: started directly it fails with error 1058 unless the service is
enabled. The plugin treats this as "no agent" — it logs the reason and keeps
working, and ssh asks for the passphrase through the Far askpass dialog. To get
a working agent, either enable the service:

```powershell
Set-Service ssh-agent -StartupType Automatic
Start-Service ssh-agent
ssh-add $env:USERPROFILE\.ssh\id_ed25519
```

or point the setting to an MSYS/Git `ssh-agent.exe`, which runs in the
foreground and prints its environment block (for example
`C:\Program Files\Git\usr\bin\ssh-agent.exe`).

### Environment variables

Variables are read from the environment of the **running Far process**. Set
them before starting Far; `setx` only affects new processes, so Far must be
restarted.

| Variable | Meaning |
|----------|---------|
| `SSHBOX_SSH_EXE` | Path to `ssh.exe`; legacy `NETBOX_SSH_EXE` is also accepted |
| `SSHBOX_SSH_CLIENT` | `putty` forces the PuTTY backend, but only when the mode is `Auto`; legacy `NETBOX_SSH_CLIENT` is also accepted |
| `SSHBOX_SSH_VERBOSE` | Adds `-o LogLevel=DEBUG1` to ssh for diagnostics; legacy `NETBOX_SSH_VERBOSE` is also accepted |

Precedence for the SSH client:

1. **Integration settings → SSH client (mode)**, when it is not `Auto`
2. `SSHBOX_SSH_CLIENT` / `NETBOX_SSH_CLIENT` (only in `Auto`)
3. `ssh.exe` availability decides between OpenSSH and PuTTY in `Auto`

Precedence for the `ssh.exe` path:

1. **Integration settings → OpenSSH client**
2. `SSHBOX_SSH_EXE` / `NETBOX_SSH_EXE`
3. `%WINDIR%\System32\OpenSSH\ssh.exe`
4. `ssh.exe` from `PATH`

Precedence for `ssh-agent.exe`:

1. **Integration settings → OpenSSH agent**
2. `ssh-agent.exe` next to the resolved `ssh.exe`
3. `%WINDIR%\System32\OpenSSH\ssh-agent.exe`
4. `ssh-agent.exe` from `PATH`

Example for the current session only:

```cmd
set SSHBOX_SSH_EXE=C:\Users\me\AppData\Local\Programs\Git\usr\bin\ssh.exe
set SSHBOX_SSH_VERBOSE=1
"C:\Far3_x64\Far.exe"
```

The plugin writes the resolved client path to the session log:

```text
OpenSSH executable: C:\...\ssh.exe
OpenSSH agent: \\.\pipe\openssh-ssh-agent
Starting OpenSSH client: ...
```

### Logging

Logging is disabled by default. Open **`Alt+Shift+W` → Logging settings**,
enable **Enable logging** and **Log to file**. The default file is:

```text
%TEMP%\<session name>.log
```

The dialog shows the actual expanded path. For key-authentication problems,
enable `SSHBOX_SSH_VERBOSE=1` and look for `identity file`, `Will attempt key`,
`Trying private key` and `Authenticated ... using "publickey"`.

## Upstream Far-NetBox

The original plugin is a file transfer plugin for
[Far Manager](https://www.farmanager.com/) supporting multiple network
protocols. Built on proven codebases — WinSCP 6.5.6, PuTTY 0.81 and FileZilla
2.2.32 — it delivers reliable remote file management inside Far's text-mode
interface.

Upstream features kept in SshBox:

- **SFTP/SCP** — SSH file transfers (in SshBox via OpenSSH)
- **FTP/FTPS** — File Transfer Protocol with TLS via FileZilla
- **WebDAV/HTTPS** — Web-based distributed authoring via neon
- **Amazon S3** — S3-compatible storage with TLS and custom CA certificates
- **Session management** — XML-based configuration with password encryption
- **Multi-architecture** — x86, x64 and ARM64 builds

## Documentation

| Guide | Description |
|-------|-------------|
| [Getting Started](docs/getting-started.md) | Build, install, and first connection |
| [Native OpenSSH](docs/openssh.md) | OpenSSH backend, `~/.ssh/config` hosts and limitations |
| [User Guide](docs/user-guide.md) | Protocols, features, and internationalization |
| [Architecture](docs/architecture.md) | Project structure, layers, and patterns |
| [Contributing](docs/contributing.md) | Code conventions and development workflow |
| [Testing](docs/testing.md) | Manual regression testing and verification |

## Credits

The original project is located at
[michaellukashov/Far-NetBox](https://github.com/michaellukashov/Far-NetBox).
This fork is maintained at
[Rickaro/Far-NetBox-Openssh](https://github.com/Rickaro/Far-NetBox-Openssh).

## License

SshBox is free software licensed under the
[GNU General Public License v3](LICENSE.txt).
