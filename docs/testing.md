[← Contributing](contributing.md) · [Back to README](../README.md)

# Testing

Far-NetBox (built as SshBox) is a Far Manager plugin. Core helpers have
automated tests under `tests/core` (`ctest`), but UI and protocol behaviour is
verified manually inside Far Manager.

## Manual Regression Testing

### Prerequisites

1. Build the project with `OPT_CREATE_PLUGIN_DIR=ON`
2. Confirm `Far3_<platform>/Plugins/SshBox/SshBox.dll` and `SshBoxAskpass.exe` exist
3. Launch `Far3_<platform>/Far.exe`
4. Press `F11` → SshBox to open the plugin

### Core Test Cases

| Feature | Steps | Expected Result |
|---------|-------|-----------------|
| New SFTP session | Create session → Set host/port → Connect | Remote panel opens with file listing |
| FTP with non-default port | Create FTP session → Set port 443 → Save/reopen | Port persists after save/load cycle |
| File download | Select file → `F5` (Copy) → Confirm | File appears in local panel |
| File upload | Select local file → `F5` → Confirm | File appears in remote panel |
| Directory creation | `F7` → Enter name | Directory created on remote host |
| Directory deletion | Select directory → `F8` → Confirm | Directory removed |
| Cancel transfer | Start large transfer → Press `Esc` → Confirm cancel | Transfer stops gracefully |
| Speed limit | Set CPS limit → Start transfer | Transfer rate limited correctly |
| Session save/load | Save session → Restart Far → Reopen session | Settings preserved |
| WebDAV TLS | Create WebDAV session → Enable HTTPS | TLS handshake succeeds |
| S3 bucket listing | Create S3 session → Connect | Bucket and object list displays |
| OpenSSH config hosts | `~/.ssh/config` with `Host`/`Include` → open plugin | Hosts listed; each `Include` file shown as a folder |
| OpenSSH key auth | Session for a `Host` with `IdentityFile` → connect | Connects with `publickey`, no password prompt |
| Askpass bridge | Session that needs a password → connect | Far password dialog appears, no extra window |
| SSH path setting | `Alt+Shift+W` → Integration settings → set `ssh.exe` | Path persisted; log shows the same `ssh.exe` |
| Settings entry | `Alt+Shift+W` → Integration settings, close and reopen | No crash (regression for 0xC0000005) |

### Protocol-Specific Tests

| Protocol | Specific Checks |
|----------|-----------------|
| **SFTP** | Key authentication, password auth, keyboard-interactive, host key caching |
| **FTP** | Active/passive mode, FTPS explicit/implicit, UTF-8 filenames |
| **SCP** | Recursive copy, preserve permissions |
| **WebDAV** | PROPFIND parsing, lock support, TLS certificate validation |
| **S3** | Bucket access, multipart upload, virtual-hosted vs path-style |

## Logging

Session logging is disabled by default. Enable it in **`Alt+Shift+W` → Logging
settings**; the default file is `%TEMP%\<session name>.log`. Set
`SSHBOX_SSH_VERBOSE=1` before starting Far to add `-o LogLevel=DEBUG1` to
OpenSSH invocations.

## Debugging in Visual Studio

1. Generate VS solution:
   ```cmd
   cmake -S . -B build-VS -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Debug -DOPT_CREATE_PLUGIN_DIR=ON
   ```
2. Open `build-VS\NetBox.sln`
3. Set debug command to `Far3_x64\Far.exe`
4. Set breakpoints in protocol code
5. Launch and trigger the affected functionality

## Build Verification

Before declaring any change complete:

- [ ] Clean build with zero warnings (MSVC W4)
- [ ] No modifications to `libs/`
- [ ] Plugin DLL in `Far3_<platform>/Plugins/SshBox/`
- [ ] CRLF line endings on all modified files
- [ ] UTF-8 without BOM
- [ ] No trailing whitespace
- [ ] Naming conventions followed (`T`/`F` prefixes)

## See Also

- [Contributing](contributing.md) — Build instructions and development workflow
- [Getting Started](getting-started.md) — Installation and first run
