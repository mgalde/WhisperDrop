# WhisperDrop

<p align="center">
  <img src="WhisperDrop.png" alt="WhisperDrop" width="200"/>
</p>

<p align="center">
  <a href="https://github.com/mgalde/WhisperDrop/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/mgalde/WhisperDrop?style=flat-square"></a>
  <img alt="License" src="https://img.shields.io/badge/license-MIT-blue?style=flat-square">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS-lightgrey?style=flat-square">
</p>

Drag and drop your audio or video files onto WhisperDrop and get a text transcript back — no cloud, no subscription, no command line required.

WhisperDrop is a graphical front-end for [OpenAI Whisper](https://github.com/openai/whisper), a free and open-source speech-recognition model that runs entirely on your own computer. Your audio never leaves your machine.

---

## What WhisperDrop does

- **Drag and drop** one or more audio or video files onto the window
- **Pick a model** — trade speed for accuracy depending on your needs
- **Choose an output format** — plain text, SRT or VTT subtitles, TSV, JSON, or all of them at once
- **Save anywhere** — next to the original file or in a folder of your choice
- **Watch live** — real-time transcription log shows progress as it runs
- **Stay up to date** — built-in update check via Help → Check for Updates

Supported file types: MP3, WAV, M4A, FLAC, AAC, OGG, OPUS, WMA, MP4, MKV, MOV, WEBM, and most other formats FFmpeg can read.

---

## System requirements

| | Linux | Windows | macOS |
|---|---|---|---|
| **OS** | Any modern desktop (GNOME, KDE, XFCE, …) | Windows 10 or later | macOS 12 (Monterey) or later |
| **Architecture** | x86_64 | x86_64 | x86_64 / Apple Silicon |
| **RAM** | 2 GB (4 GB+ recommended for larger models) | 2 GB | 2 GB |
| **Disk** | 1 GB free for `base`; up to 10 GB for `large-v3` | 1 GB free | 1 GB free |
| **Internet** | Required on first use to download model weights; not needed after that | Same | Same |

> **Privacy:** Once the model is downloaded, WhisperDrop works completely offline. No audio, text, or telemetry is ever sent anywhere.

---

## Installing WhisperDrop

- [Linux](#linux)
- [Windows](#windows)
- [macOS](#macos)

---

### Linux

#### Option 1 — Graphical Installer (Recommended)

The installer checks your system, installs anything that is missing, copies WhisperDrop into place, and adds it to your Applications menu. No terminal required.

1. Go to the [Releases page](https://github.com/mgalde/WhisperDrop/releases/latest)
2. Download **WhisperDrop-Installer-vX.X.X-linux-x86_64** and make it executable:
   ```bash
   chmod +x WhisperDrop-Installer-*-linux-x86_64
   ./WhisperDrop-Installer-*-linux-x86_64
   ```
3. Follow the on-screen steps — you may be asked for your password once to install system packages
4. Click **Launch WhisperDrop** when the installer finishes

WhisperDrop will appear in your Applications menu under Audio & Video.

> **Removing WhisperDrop:** Download **WhisperDrop-Uninstaller-vX.X.X-linux-x86_64** from the same release and run it. It will remove the application, shortcut, and icon. You can optionally remove saved settings too.

> **Fedora note:** FFmpeg is in the RPM Fusion repository, which is not enabled by default. If the installer fails to install FFmpeg, enable RPM Fusion first:
> ```bash
> sudo dnf install https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
> ```
> Then run the installer again.

#### Option 2 — Pre-built binary

If you prefer to manage dependencies yourself, download the standalone binary and run it directly.

**Step 1 — Install FFmpeg**

| Distro | Command |
|---|---|
| **Arch / CachyOS / Manjaro** | `sudo pacman -S ffmpeg` |
| **Ubuntu / Debian** | `sudo apt install ffmpeg` |
| **Fedora** | See Fedora note above |
| **openSUSE** | `sudo zypper install ffmpeg` |

**Step 2 — Install Whisper**

```bash
pip install -U openai-whisper
```

> On Arch, Ubuntu 22.04+, and other distros with an externally managed Python environment, add `--break-system-packages` if pip refuses.

Verify it is working:
```bash
whisper --help
```

**Step 3 — Download and run WhisperDrop**

1. Go to the [Releases page](https://github.com/mgalde/WhisperDrop/releases/latest)
2. Download **WhisperDrop-vX.X.X-linux-x86_64**
3. Make it executable and run:
   ```bash
   chmod +x WhisperDrop-*-linux-x86_64
   ./WhisperDrop-*-linux-x86_64
   ```

The binary links against a small number of standard libraries present on any GTK-based desktop:

| Library | Install if missing |
|---|---|
| GTK4 | `sudo pacman -S gtk4` / `sudo apt install libgtk-4-1` |
| libsoup3 | `sudo pacman -S libsoup3` / `sudo apt install libsoup-3.0-0` |
| json-glib | `sudo pacman -S json-glib` / `sudo apt install libjson-glib-1.0-0` |

On any GNOME or KDE Plasma desktop these are already present.

#### Option 3 — Build from source

**1. Install build dependencies**

| Distro | Command |
|---|---|
| **Arch / CachyOS / Manjaro** | `sudo pacman -S gtk4 libsoup3 json-glib meson ninja gcc` |
| **Ubuntu / Debian** | `sudo apt install libgtk-4-dev libsoup-3.0-dev libjson-glib-dev meson ninja-build gcc` |
| **Fedora** | `sudo dnf install gtk4-devel libsoup3-devel json-glib-devel meson ninja-build gcc` |
| **openSUSE** | `sudo zypper install gtk4-devel libsoup-devel libjson-glib-devel meson ninja gcc` |

**2. Clone and build**

```bash
git clone https://github.com/mgalde/WhisperDrop.git
cd WhisperDrop
meson setup build
ninja -C build
```

This produces three executables in `build/`:
- `WhisperDrop` — the main application
- `installer/whisperdrop-installer` — graphical installer
- `installer/whisperdrop-uninstaller` — graphical uninstaller

**3. Run**

```bash
./build/WhisperDrop
```

---

### Windows

> **Pre-built binaries** are available on the [Releases page](https://github.com/mgalde/WhisperDrop/releases/latest).
> Download **WhisperDrop-Installer-vX.X.X-windows-x86_64.exe** and run it.

**Before running the installer, install these prerequisites:**

**1. Install Python**

Download from [python.org](https://www.python.org/downloads/). During installation, check **"Add Python to PATH"**.

**2. Install FFmpeg**

Using winget (Windows 10 20H2+):
```powershell
winget install Gyan.FFmpeg
```
Or download from [ffmpeg.org](https://ffmpeg.org/download.html) and add the `bin` folder to your PATH.

**3. Run the installer**

The installer will check that Python and FFmpeg are present, install the Whisper transcription engine via pip, copy WhisperDrop to `%LOCALAPPDATA%\WhisperDrop\`, and add it to your Start Menu.

**Build from source (MSYS2)**

```bash
# In an MSYS2 MinGW-w64 shell:
pacman -S mingw-w64-x86_64-gtk4 mingw-w64-x86_64-libsoup3 \
          mingw-w64-x86_64-json-glib mingw-w64-x86_64-meson \
          mingw-w64-x86_64-ninja mingw-w64-x86_64-gcc

git clone -b windows https://github.com/mgalde/WhisperDrop.git
cd WhisperDrop
meson setup build
ninja -C build
```

> Requires GLib ≥ 2.74. The self-update feature opens the GitHub releases page on Windows rather than replacing the running binary.

---

### macOS

> **Pre-built binaries** are available on the [Releases page](https://github.com/mgalde/WhisperDrop/releases/latest).
> Download **WhisperDrop-vX.X.X-macos-x86_64**, make it executable, and run it.

**Before running, install dependencies:**

**1. Install Homebrew** (if not already installed)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

**2. Install FFmpeg and Whisper**

```bash
brew install ffmpeg
pip install -U openai-whisper
```

**Build from source (Homebrew)**

```bash
brew install gtk4 libsoup json-glib meson ninja

git clone -b macos https://github.com/mgalde/WhisperDrop.git
cd WhisperDrop
meson setup build
ninja -C build
./build/WhisperDrop
```

> macOS ships a bare binary only — no graphical installer. The binary can be placed anywhere and run directly.

---

## Using WhisperDrop

### First launch

On first use, Whisper will download the model weights for whichever model you selected. This is a one-time download per model:

| Model | Download size |
|---|---|
| `tiny` / `base` | ~75–150 MB |
| `small` | ~480 MB |
| `medium` | ~1.5 GB |
| `turbo` | ~1.6 GB |
| `large-v3` | ~3.1 GB |

WhisperDrop must be connected to the internet for this step. After the model is cached, it works fully offline.

### Step by step

1. Launch WhisperDrop from your Applications menu or by running the binary
2. Drag audio or video files into the drop zone, or click **Add Files…**
3. Pick a **Model** — `turbo` is the default and a good balance of speed and accuracy
4. Pick an **Output format** — see the table below
5. Choose where to save — **Save next to input file** is the simplest option
6. Click **Start**
7. Watch the log for progress; click **Open Output Folder** when done

### Model guide

| Model | Speed | Accuracy | RAM needed | Best for |
|---|---|---|---|---|
| `tiny` | Fastest | Basic | ~1 GB | Quick drafts, clear speech |
| `base` | Fast | Decent | ~1 GB | Casual use |
| `small` | Moderate | Good | ~2 GB | Most everyday audio |
| `medium` | Slower | Better | ~5 GB | Accents, noisy audio |
| `turbo` | Fast | Very good | ~6 GB | Best speed/quality balance |
| `large-v3` | Slowest | Best | ~10 GB | Maximum accuracy |

### Output formats

| Format | Extension | Use it for |
|---|---|---|
| Plain text | `.txt` | Reading, copying into documents |
| SRT subtitles | `.srt` | Adding subtitles in video editors (Premiere, DaVinci, etc.) |
| VTT subtitles | `.vtt` | Web video subtitles (HTML `<track>`) |
| Tab-separated | `.tsv` | Importing timestamps into spreadsheets |
| JSON | `.json` | Processing transcripts programmatically |
| All | — | Produces every format at once |

---

## Troubleshooting

**"whisper: command not found" after clicking Start**
Whisper is not on your PATH. Install it with `pip install -U openai-whisper` and make sure `~/.local/bin` is in your PATH. You can verify with `whisper --help` in a terminal.

**"ffmpeg: command not found" in the log**
FFmpeg is not installed. See the installation steps for your distro above.

**Transcription is very slow**
This is normal for larger models on CPU. Consider switching to `tiny` or `base` for faster results, or `turbo` if you have enough RAM. GPU acceleration is not currently supported through the WhisperDrop UI.

**The application won't start after installing the binary**
The binary requires GTK4, libsoup3, and json-glib. See the library table under Option 2 above and install any that are missing.

**No Applications menu entry after running the installer**
Log out and back in, or run `update-desktop-database ~/.local/share/applications` in a terminal. Some desktop environments need a restart to pick up new entries.

**The installer fails to install a system package**
The installer uses `pkexec` to elevate privileges. If your system does not have a polkit agent running (common in minimal or tiling-WM setups), the password prompt will not appear. In that case, install the missing packages manually and re-run the installer, or use Option 2.

---

## Checking for updates

Go to **Help → Check for Updates** in the menu bar. If a newer version is available on GitHub, WhisperDrop will offer to download and replace itself automatically.

---

## Requirements summary

| Requirement | Purpose | Linux | Windows | macOS |
|---|---|---|---|---|
| FFmpeg | Audio decoding | Required | Required | Required |
| openai-whisper | Transcription engine | Required | Required | Required |
| Python | Needed to run Whisper | Usually present | Required | Usually present |
| GTK4 | UI framework | Required | Bundled (MSYS2) | Required (Homebrew) |
| libsoup3 | HTTPS (update check) | Required | Bundled | Required (Homebrew) |
| json-glib | JSON parsing (update check) | Required | Bundled | Required (Homebrew) |
| Meson + Ninja + GCC | Build toolchain | Source builds only | Source builds only | Source builds only |

---

## License

MIT License — see [LICENSE](LICENSE) for details.

If you use WhisperDrop and find it useful, feel free to reach out at **WhisperDrop@saguarosec.com** — we'd love to hear from you.
