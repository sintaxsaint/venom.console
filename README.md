# venom.console v3.0

A powerful cross-platform terminal console written in C.
**Windows, Linux, macOS. No external libraries required.**

---

## Build

### Linux / macOS
```bash
gcc venom_console.c -o venom_console -lm
chmod +x venom
```

### Windows (MinGW)
```bash
gcc venom_console.c -o venom_console.exe -lws2_32
```

### Windows (MSVC)
```cmd
cl venom_console.c /Fe:venom_console.exe /link ws2_32.lib iphlpapi.lib
```

---

## Elevation — Fully Disclosed

venom.console requests administrator / root privileges on every launch.
This is intentional, fully disclosed, and required for core features:
hosts file editing, process management, network configuration,
power commands (shutdown/suspend), and raw socket operations.

**This is not hidden. It is documented here and shown at launch.**

### Windows
Embed the manifest so the OS requests UAC automatically:
```cmd
mt.exe -manifest venom_console.manifest -outputresource:venom_console.exe;1
```
Windows will show a standard UAC prompt when the user launches venom.console.
No tricks. One prompt. Done.

### Linux / macOS
Use the `venom` launcher wrapper:
```bash
./venom
```
The wrapper calls `sudo` if not already root. Your password is prompted
by the OS as normal. You can read exactly what it does in `venom`.

---

## First Run

```
./venom
> setup
```

The setup wizard walks you through:
1. System check (platform, admin status)
2. AI provider selection (Ollama local / HuggingFace / Remote)
3. Automatic Ollama install and model download
4. Config file written

Reset config at any time:
```
> setup reset
```

---

## AI System

Three providers, switchable via config:

| Provider | Command | Notes |
|----------|---------|-------|
| Ollama (local) | `config set ai_provider ollama` | Free, private, offline |
| HuggingFace | `config set ai_provider huggingface` | Free tier, remote |
| Remote/Custom | `config set ai_provider remote` | Minimax, OpenAI, Groq, etc. |

### Ollama (local AI)
```
> ollama recommended        — see model list with sizes
> ollama pull llama3.2      — download a model
> ollama status             — check if Ollama is running
> chatbot                   — start chatting
```

Recommended models:

| Model | Size | Notes |
|-------|------|-------|
| llama3.2 | 2GB | Meta — great all-rounder |
| mistral | 4GB | Fast and smart |
| phi3 | 2GB | Microsoft — lightweight |
| gemma2 | 5GB | Google — very capable |
| tinyllama | 638MB | Ultra-light |
| codellama | 4GB | Code-focused |
| deepseek-r1 | 4GB | Strong reasoning |

### Remote Provider Presets
```
> setup       → choose preset during wizard
```
Or manually:
```
> config set remote_endpoint https://api.minimax.chat/v1
> config set remote_model MiniMax-Text-01
> config set remote_key sk_yourkey
> config set ai_provider remote
> chatbot
```

Supported presets: HuggingFace, Minimax, OpenAI, Groq, or any custom OpenAI-compatible endpoint.

---

## Config System

Config file locations:
- **Windows**: `%APPDATA%\venom_console\venom.cfg`
- **Linux/macOS**: `~/.config/venom_console/venom.cfg`

Plain `key=value` text — editable by hand or via console:

```
> config show                    — view all settings (keys masked)
> config set <key> <value>       — change a value
> config clear <key>             — clear a value
> config path                    — show config file path
```

### All config keys

| Key | Default | Description |
|-----|---------|-------------|
| `hf_key` | — | HuggingFace API key |
| `ai_provider` | ollama | `ollama` / `huggingface` / `remote` |
| `ollama_host` | localhost:11434 | Ollama server address |
| `ollama_model` | llama3.2 | Default local model |
| `remote_endpoint` | — | Remote API URL |
| `remote_key` | — | Remote API key |
| `remote_model` | — | Remote model name |
| `matrix_color` | rainbow | `rainbow/green/red/cyan/white/blue` |
| `matrix_speed` | 5 | 1 (slow) — 10 (fast) |
| `matrix_chars` | binary | `binary/ascii/hex` |
| `ls_colors` | 1 | Colored `ls` output |
| `tree_style` | box | `box` (unicode) or `plain` |
| `util_sidebyside` | 1 | Show before/after for text tools |
| `guid_count` | 1 | Default number of GUIDs |
| `calc_precision` | 4 | Decimal places for calc |
| `timer_style` | 1 | `0`=number, `1`=progress bar |
| `typewriter_speed` | 50 | ms per character |
| `serve_port` | 8080 | Default HTTP server port |
| `serve_dir` | . | Default serve directory |
| `serve_autostart` | 0 | Auto-restore server on launch |
| `pin` | — | Kill switch PIN |

---

## Kill Switch

PIN-protected emergency lockout system.

```
> killswitch set-pin          — set a PIN (4+ chars)
> killswitch arm              — arm the switch
> killswitch trigger          — LOCK venom immediately, exit
> killswitch disarm           — remove lock (PIN required)
> killswitch status           — show state
> ks                          — alias
```

When triggered:
- A `.venom_dead` lock file is written to the working dir and config dir
- venom.console refuses to start while the lock file exists
- Disarm by running `killswitch disarm` (PIN required) or deleting the lock file manually

---

## Local HTTP Server

```
> serve                       — serve current dir on port 8080
> serve 3000                  — custom port
> serve 3000 ./public         — custom port and directory
> serve restore               — restart last server session
```

Features:
- Live request log with method, path, status, size
- Correct MIME types (HTML, CSS, JS, JSON, images, PDF)
- Directory listing fallback
- State file saved — `serve restore` brings it back after a restart
- `serve_autostart = 1` warns you on launch if the server was previously running

---

## Command Reference

### Core
`help` `cls` `version` `echo` `exit/q`

### System
`sysinfo` `uptime` `whoami` `whereami` `env` `storage` `battery`
`processes/ps` `kill` `isadmin`
`shutdown` `restart` `lock` `logoff` `hibernate` `suspend`

### Files
`ls` `cd` `cat` `mkdir` `rmdir` `copy/cp` `move/mv` `del/rm` `touch`
`find` `tree` `head` `tail` `wc` `filesize` `compress/zip` `extract/unzip`
`hexplain`

### Hashing
`md5` `sha256/sha` `sha512`

### Network — Core
`ping` `tracert` `ns` `netstat` `arp` `hostname` `dnsflush`
`myip` `get-ip/ip` `wifi` `netinfo` `netmon`
`blocksite/block` `unblocksite/unblock`

### Network — Port & IP Tools
`portscan` `tcping` `whois` `geoip`
`portinfo <port>` — look up service name for a port number
`localports` — ports your machine is currently listening on
`banner <host> <port>` — grab service banner from an open port
`reachable <host>` — quick yes/no reachability check
`ipcalc <ip> <prefix>` — subnet calculator
`rdns <ip>` — reverse DNS lookup
`dnscheck <domain>` — full DNS record dump
`interfaces` — list all network interfaces
`routetable` — full routing table

### Local Server
`serve [port] [dir]` `serve restore`

### Encoding
`b64enc` `b64dec` `hexenc/hex` `hexdec/unhex` `rot13` `urlenc` `urldec`

### Text & Utils
`upper` `lower` `rev` `guid [N]` `calc <expr>`
`wc` `timer <s>` `typewriter [fast|slow] <text>` `clock` `lipsum [N]`
`clipcopy` `clippaste`

### Fun
`matrix [fast|slow|1-10]` — rainbow by default, configurable color, speed, and character set

### AI
`chatbot/ai` — chat with configured AI provider
`ollama status/list/pull/recommended/set-default/set-host`

### Setup & Config
`setup` `setup reset`
`config show/set/clear/path`
`cfg` `settings` — aliases for `config`

### History
`history [search]`

### Kill Switch
`killswitch status/set-pin/arm/trigger/disarm`
`ks` — alias

---

## What's New in v3.0

- **Matrix** — rainbow color, configurable speed (1-10), character sets (binary/ascii/hex), `fast`/`slow` presets
- **AI system** — Ollama local models, HuggingFace, and any remote OpenAI-compatible provider
- **Setup wizard** — `setup` installs Ollama, pulls a model, and writes the full config in one flow
- **Local HTTP server** — `serve` with live request log, MIME types, session restore, autostart
- **Port & IP tools** — `ipcalc`, `rdns`, `portinfo`, `localports`, `banner`, `reachable`, `dnscheck`, `interfaces`, `routetable`, `netmon`
- **Upgraded utils** — side-by-side display, expression calculator, progress bar timer, coloured ls, box-drawing tree, numbered cat
- **Config system** — all settings editable from the terminal, stored in OS-appropriate location
- **Kill switch** — PIN-protected emergency lockout with dead-man file
- **Elevation** — Windows manifest + Linux sudo wrapper, fully disclosed
