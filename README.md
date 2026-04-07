# WhisperDrop

<p align="center">
  <img src="WhisperDrop.png" alt="WhisperDrop" width="720"/>
</p>

<p align="center">
  <a href="https://github.com/mgalde/WhisperDrop/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/mgalde/WhisperDrop?style=flat-square"></a>
  <img alt="License" src="https://img.shields.io/badge/license-MIT-blue?style=flat-square">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Linux-lightgrey?style=flat-square">
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

| | Minimum |
|---|---|
| **OS** | Any modern Linux desktop (GNOME, KDE, XFCE, …) |
| **Architecture** | x86_64 |
| **RAM** | 2 GB (4 GB+ recommended for larger models) |
| **Disk** | 1 GB free for the `base` model; up to 10 GB for `large-v3` |
| **Internet** | Required on first use to download the Whisper model weights; not needed after that |

> **Privacy:** Once the model is downloaded, WhisperDrop works completely offline. No audio, text, or telemetry is ever sent anywhere.

---

## Installing WhisperDrop

### Option 1 — Graphical Installer (Recommended)

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

---

### Option 2 — Pre-built binary

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

---

### Option 3 — Build from source

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

| Requirement | Purpose | Needed for |
|---|---|---|
| FFmpeg | Audio decoding | Running WhisperDrop |
| openai-whisper | Transcription engine | Running WhisperDrop |
| GTK4 | UI framework | Running WhisperDrop |
| libsoup3 | HTTPS (update check) | Running WhisperDrop |
| json-glib | JSON parsing (update check) | Running WhisperDrop |
| Meson + Ninja + GCC | Build toolchain | Building from source only |

---

## License

MIT License — see [LICENSE](LICENSE) for details.

If you use WhisperDrop and find it useful, feel free to reach out at **WhisperDrop@saguarosec.com** — we'd love to hear from you.
