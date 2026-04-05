# WhisperDrop

<p align="center">
  <img src="WhisperDrop.png" alt="WhisperDrop" width="720"/>
</p>

A simple Linux desktop application that lets you drag and drop audio or video files and get text transcripts back — no command line required.

WhisperDrop is a graphical front-end for [OpenAI Whisper](https://github.com/openai/whisper), a free and open-source speech recognition model that runs entirely on your own computer. Nothing is sent to the cloud.

---

## What it does

- Drag and drop one or more audio or video files onto the window
- Choose which Whisper model to use (larger = more accurate, slower)
- Choose your output format: plain text, SRT subtitles, VTT, TSV, JSON, or all of them
- Save transcripts next to your original files or to a folder of your choice
- Watch the transcription log in real time
- Check for app updates from the Help menu

Supported file types include: MP3, WAV, M4A, FLAC, AAC, OGG, OPUS, WMA, MP4, MKV, MOV, WEBM, and more.

---

## Before you install WhisperDrop

WhisperDrop is a graphical wrapper — it calls Whisper and FFmpeg behind the scenes. You need both installed before launching the app.

### 1. Install FFmpeg

FFmpeg handles audio decoding. Whisper will not work without it.

| Distro | Command |
|---|---|
| **Arch / CachyOS / Manjaro** | `sudo pacman -S ffmpeg` |
| **Ubuntu / Debian** | `sudo apt install ffmpeg` |
| **Fedora** | See note below |
| **openSUSE** | `sudo zypper install ffmpeg` |

> **Fedora:** FFmpeg requires the RPM Fusion repository first:
> ```bash
> sudo dnf install https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
> sudo dnf install ffmpeg
> ```

### 2. Install Whisper

Whisper is a Python package that provides the `whisper` command WhisperDrop calls.

```bash
pip install -U openai-whisper
```

> On systems with a managed Python environment (Arch, Ubuntu 22.04+) you may need to use a virtual environment or add `--break-system-packages`.

**Verify it works:**
```bash
whisper --help
```

If you see a help message, you are ready to go.

---

## Installing WhisperDrop

### Option A — Download the pre-built binary (easiest)

1. Go to the [Releases page](https://github.com/mgalde/WhisperDrop/releases/latest)
2. Download `WhisperDrop-vX.X.X-linux-x86_64`
3. Make it executable and run it:

```bash
chmod +x WhisperDrop-v*-linux-x86_64
./WhisperDrop-v*-linux-x86_64
```

The binary is self-contained — no Python installation is needed to run it. It does link against a few standard system libraries that are present on any modern Linux desktop:

| Library | Install if missing |
|---|---|
| GTK4 (`libgtk-4.so.1`) | `sudo pacman -S gtk4` / `sudo apt install libgtk-4-1` |
| libsoup3 (`libsoup-3.0.so.0`) | `sudo pacman -S libsoup3` / `sudo apt install libsoup-3.0-0` |
| json-glib (`libjson-glib-1.0.so.0`) | `sudo pacman -S json-glib` / `sudo apt install libjson-glib-1.0-0` |

On any GNOME or GTK-based desktop these will already be installed.

---

### Option B — Build from source

**1. Install build dependencies:**

| Distro | Command |
|---|---|
| **Arch / CachyOS / Manjaro** | `sudo pacman -S gtk4 libsoup3 json-glib meson ninja` |
| **Ubuntu / Debian** | `sudo apt install libgtk-4-dev libsoup-3.0-dev libjson-glib-dev meson ninja-build gcc` |
| **Fedora** | `sudo dnf install gtk4-devel libsoup3-devel json-glib-devel meson ninja-build gcc` |
| **openSUSE** | `sudo zypper install gtk4-devel libsoup-devel libjson-glib-devel meson ninja gcc` |

**2. Clone and build:**

```bash
git clone https://github.com/mgalde/WhisperDrop.git
cd WhisperDrop
meson setup build
ninja -C build
```

**3. Run:**

```bash
./build/WhisperDrop
```

---

## Usage

1. Launch WhisperDrop
2. Drag audio or video files into the drop zone, or click **Add Files…**
3. Select your **Model** — `turbo` is the default and works well for most uses
4. Select your **Output format** — `txt` for plain text, `srt` for subtitle timecodes
5. Choose where to save: **Save next to input file** keeps things simple, or pick a specific folder
6. Click **Start**
7. When finished, click **Open Output Folder** to find your transcripts

### Model guide

| Model | Speed | Accuracy | RAM needed |
|---|---|---|---|
| `tiny` | Fastest | Lower | ~1 GB |
| `base` | Fast | Decent | ~1 GB |
| `small` | Moderate | Good | ~2 GB |
| `medium` | Slower | Better | ~5 GB |
| `turbo` | Fast | Very good | ~6 GB |
| `large-v3` | Slowest | Best | ~10 GB |

---

## Checking for updates

Go to **Help → Check for Updates** in the menu bar. If a newer version is available on GitHub, the app will offer to download and install it automatically.

---

## Requirements summary

| Requirement | Purpose | Needed for |
|---|---|---|
| FFmpeg | Audio decoding | Runtime |
| openai-whisper | Transcription engine | Runtime |
| GTK4 | GUI framework | Runtime |
| libsoup3 | HTTPS (update check) | Runtime |
| json-glib | JSON parsing | Runtime |
| Meson + Ninja + GCC | Build toolchain | Build from source only |

---

## License

MIT License — see [LICENSE](LICENSE) for details.
