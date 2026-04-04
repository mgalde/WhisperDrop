# WhisperDrop GUI (drag & drop Whisper)

This is a small desktop GUI that wraps the `whisper` CLI from **openai/whisper**.

## What it does
- Drag & drop media files to transcribe
- Pick the model (defaults to `turbo`)
- Choose output format (txt/srt/vtt/tsv/json/all)
- Choose output folder (or write next to the input)
- Run without typing CLI commands

## Requirements (Linux/CachyOS)
- Python 3.10+ recommended
- `whisper` installed and on PATH (you already have this)
- `ffmpeg` installed (Whisper needs it)
- PySide6 (Qt)

## Install + run (recommended: venv)
```bash
python -m venv .venv
source .venv/bin/activate
pip install -U pip
pip install PySide6
# whisper is already installed for you; if not:
# pip install -U openai-whisper
python whisperdrop_gui.py
```

## Optional: make it feel like a “real app”
Create a desktop launcher:
- Copy `whisperdrop_gui.py` somewhere stable, e.g. `~/Apps/WhisperDrop/`
- Create `~/.local/share/applications/whisperdrop.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=WhisperDrop
Comment=Drag-and-drop GUI for Whisper transcription
Exec=/home/YOU/Apps/WhisperDrop/.venv/bin/python /home/YOU/Apps/WhisperDrop/whisperdrop_gui.py
Icon=audio-x-generic
Terminal=false
Categories=AudioVideo;Utility;
```

Then refresh your desktop menu / search.

## Packaging (optional)
If you want a single binary (still uses your installed `whisper`):
```bash
pip install pyinstaller
pyinstaller --onefile --windowed whisperdrop_gui.py
```

## Updates (optional)
This starter app includes a “Check for Updates” menu item, but you must configure it:
- If you publish to GitHub, set `GITHUB_REPO = "youruser/yourrepo"` in the source.
- Or if you publish to PyPI, set `PIP_PACKAGE = "yourpackagename"` to allow in-app pip upgrades.
