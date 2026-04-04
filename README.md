# WhisperDrop

<p align="center">
  <img src="WhisperDrop.png" alt="WhisperDrop" width="720"/>

A simple desktop application that lets you drag and drop audio or video files and get text transcripts back — no command line required.

WhisperDrop is a graphical front-end for [OpenAI Whisper](https://github.com/openai/whisper), a free and open-source speech recognition model that runs entirely on your own computer. Nothing is sent to the cloud.
</p>

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

WhisperDrop is a graphical wrapper — it calls Whisper and FFmpeg behind the scenes. You need both installed first.

### 1. Install FFmpeg

FFmpeg handles audio decoding. Whisper will not work without it.

| OS / Distro | Command |
|---|---|
| **Ubuntu / Debian** | `sudo apt install ffmpeg` |
| **Fedora** | `sudo dnf install ffmpeg` |
| **Arch / CachyOS / Manjaro** | `sudo pacman -S ffmpeg` |
| **openSUSE** | `sudo zypper install ffmpeg` |
| **macOS (Homebrew)** | `brew install ffmpeg` |
| **Windows** | Download from [ffmpeg.org](https://ffmpeg.org/download.html) and add to your PATH |

### 2. Install Whisper

Whisper is a Python package. You need Python 3.9 or newer installed first.

**Check if you have Python:**
```bash
python3 --version   # Linux / macOS
python --version    # Windows
```

**Install Python if you don't have it:**

| OS / Distro | Command or link |
|---|---|
| **Ubuntu / Debian** | `sudo apt install python3 python3-pip` |
| **Fedora** | `sudo dnf install python3 python3-pip` |
| **Arch / CachyOS / Manjaro** | `sudo pacman -S python python-pip` |
| **openSUSE** | `sudo zypper install python3 python3-pip` |
| **macOS** | `brew install python` or download from [python.org](https://python.org) |
| **Windows** | Download from [python.org](https://python.org) — check "Add to PATH" during install |

**Install Whisper:**
```bash
pip install -U openai-whisper
```

> On some Linux systems you may need `pip3` instead of `pip`, or you may need to use a virtual environment (see the **Run from source** section below).

**Verify it works:**
```bash
whisper --help
```

If you see a help message, you are ready to go.

---

## Installing WhisperDrop

### Option A — Download the pre-built executable (easiest)

1. Go to the [Releases page](https://github.com/mgalde/WhisperDrop/releases/latest)
2. Download the file for your platform
3. Make it executable and run it

**Linux:**
```bash
chmod +x WhisperDrop
./WhisperDrop
```

**macOS:** Right-click the app → Open (needed the first time to bypass Gatekeeper)

**Windows:** Double-click `WhisperDrop.exe`

No Python installation is required to run the pre-built executable.

---

### Option B — Run from source

This works on any OS with Python 3.9+ and is useful if you want to modify the app.

**1. Clone the repository:**
```bash
git clone https://github.com/mgalde/WhisperDrop.git
cd WhisperDrop
```

**2. Create a virtual environment (recommended):**

```bash
# Linux / macOS
python3 -m venv .venv
source .venv/bin/activate

# Windows
python -m venv .venv
.venv\Scripts\activate
```

**3. Install dependencies:**
```bash
pip install -r requirements.txt
```

**4. Run the app:**
```bash
python whisperdrop_gui_fixed.py
```

---

### Option C — Build the executable yourself

If you want to produce your own binary from source:

```bash
pip install pyinstaller
pyinstaller --onefile --windowed --name WhisperDrop whisperdrop_gui_fixed.py
```

The output will be in the `dist/` folder.

---

## Usage

1. Launch WhisperDrop
2. Drag audio or video files into the drop zone, or click **Add Files…**
3. Select your **Model** — `turbo` is the default and works well for most uses. For higher accuracy use `large-v3` (slower and requires more RAM)
4. Select your **Output format** — `txt` gives plain text, `srt` gives subtitle timecodes
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

Go to **Help → Check for Updates** in the menu bar. If a newer version is available on GitHub, the app will offer to open the download page.

---

## Distro-specific notes

### Arch Linux / CachyOS / Manjaro
Arch uses a system-managed Python environment. The safest way to run from source is with a virtual environment:
```bash
python -m venv --system-site-packages .venv
source .venv/bin/activate
pip install PySide6 openai-whisper
python whisperdrop_gui_fixed.py
```

### Ubuntu / Debian
On Ubuntu 22.04+ you may see an `externally-managed-environment` error with pip. Use a virtual environment as shown above, or add `--break-system-packages` if you know what you are doing.

### Fedora
Fedora ships Python 3 by default. FFmpeg may require enabling the RPM Fusion repository first:
```bash
sudo dnf install https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
sudo dnf install ffmpeg
```

### Windows
- Install Python from [python.org](https://python.org) — make sure to check **Add Python to PATH**
- Install FFmpeg and add it to your PATH (see [this guide](https://www.wikihow.com/Install-FFmpeg-on-Windows))
- Then follow Option A (download the .exe) or Option B (run from source)

### macOS
- Install Homebrew from [brew.sh](https://brew.sh) if you do not have it
- `brew install python ffmpeg`
- Then `pip3 install openai-whisper`
- Download the macOS release from the Releases page or run from source

---

## Requirements summary

| Requirement | Purpose |
|---|---|
| Python 3.9+ | Required to run from source |
| FFmpeg | Audio decoding — Whisper needs this |
| openai-whisper | The transcription engine |
| PySide6 | The GUI framework (run from source only) |

---

## License

MIT License — see [LICENSE](LICENSE) for details.
