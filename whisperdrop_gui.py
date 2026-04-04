#!/usr/bin/env python3
"""
WhisperDrop GUI
A drag-and-drop desktop wrapper around the `whisper` CLI (openai/whisper).

Features:
- Drag & drop one or more audio/video files
- Choose Whisper model (defaults to turbo)
- Choose output folder (or same as input folder)
- Choose output format (txt/srt/vtt/tsv/json/all)
- Start/stop queue
- Simple "check for updates" (GitHub Releases) + optional pip update

Notes:
- This GUI calls the `whisper` executable already installed on your system.
- Install dependencies: PySide6 (Qt).
"""
from __future__ import annotations

import os
import sys
import shlex
import shutil
import subprocess
import threading
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

from PySide6 import QtCore, QtGui, QtWidgets
from PySide6.QtCore import Qt, QUrl
from PySide6.QtGui import QAction
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QFileDialog, QListWidget, QListWidgetItem,
    QComboBox, QCheckBox, QPlainTextEdit, QMessageBox, QProgressBar,
    QLineEdit, QGroupBox, QFormLayout
)
from PySide6.QtGui import QDesktopServices


APP_NAME = "WhisperDrop"
APP_VERSION = "0.1.0"

# ---- Update config (optional) ----
# If you publish this on GitHub, set:
#   GITHUB_REPO = "YOURUSER/whisperdrop"
# and the app can check "latest release" via GitHub API.
GITHUB_REPO = ""  # e.g., "mike-galde/whisperdrop"

# If you install this via pip (as a package you publish), set:
#   PIP_PACKAGE = "whisperdrop"
PIP_PACKAGE = ""


DEFAULT_MODELS = [
    "turbo",
    "tiny", "tiny.en",
    "base", "base.en",
    "small", "small.en",
    "medium", "medium.en",
    "large", "large-v2", "large-v3",
]

OUTPUT_FORMATS = ["all", "txt", "srt", "vtt", "tsv", "json"]

# Common media extensions Whisper can handle (ffmpeg dependent)
MEDIA_FILTER = "Audio/Video (*.mp3 *.wav *.m4a *.flac *.aac *.ogg *.opus *.wma *.mp4 *.mkv *.mov *.webm *.m4v);;All files (*.*)"


@dataclass
class Job:
    path: str
    status: str = "Queued"  # Queued/Running/Done/Error/Cancelled
    out_dir: Optional[str] = None
    error: Optional[str] = None


class DropArea(QGroupBox):
    files_dropped = QtCore.Signal(list)

    def __init__(self, parent=None):
        super().__init__("Drop files here", parent)
        self.setAcceptDrops(True)
        self.setMinimumHeight(110)

        lay = QVBoxLayout(self)
        lbl = QLabel("Drag & drop audio/video files into this box.\n(or use “Add Files…”)")
        lbl.setAlignment(Qt.AlignCenter)
        lbl.setStyleSheet("QLabel { font-size: 14px; }")
        lay.addWidget(lbl)

    def dragEnterEvent(self, event: QtGui.QDragEnterEvent) -> None:
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            event.ignore()

    def dropEvent(self, event: QtGui.QDropEvent) -> None:
        urls = event.mimeData().urls()
        paths = []
        for u in urls:
            if u.isLocalFile():
                paths.append(u.toLocalFile())
        if paths:
            self.files_dropped.emit(paths)
        event.acceptProposedAction()


class WhisperWorker(QtCore.QObject):
    # Signals for UI updates
    log = QtCore.Signal(str)
    job_update = QtCore.Signal(int, str)  # row, new_status
    progress = QtCore.Signal(int)         # 0..100 (coarse)
    done = QtCore.Signal()
    fatal = QtCore.Signal(str)

    def __init__(self, jobs: List[Job], model: str, out_format: str, same_folder: bool,
                 out_folder: str, extra_args: str):
        super().__init__()
        self.jobs = jobs
        self.model = model.strip()
        self.out_format = out_format.strip()
        self.same_folder = same_folder
        self.out_folder = out_folder.strip()
        self.extra_args = extra_args.strip()
        self._cancel = threading.Event()
        self._proc: Optional[subprocess.Popen] = None

    def cancel(self):
        self._cancel.set()
        if self._proc and self._proc.poll() is None:
            try:
                self._proc.terminate()
            except Exception:
                pass

    def _whisper_cmd(self, input_path: str, output_dir: str) -> List[str]:
        exe = shutil.which("whisper")
        if not exe:
            raise FileNotFoundError("Could not find `whisper` on PATH.")
        cmd = [exe, input_path, "--model", self.model, "--output_format", self.out_format, "--output_dir", output_dir]
        if self.extra_args:
            # Keep it simple: split like a shell would (respects quotes)
            cmd += shlex.split(self.extra_args)
        return cmd

    @QtCore.Slot()
    def run(self):
        try:
            # Sanity check before starting
            if not shutil.which("whisper"):
                self.fatal.emit(
                    "I can’t find the `whisper` command.\n\n"
                    "Install it first (openai/whisper) so the `whisper` executable is on your PATH, "
                    "then reopen this app."
                )
                return

            total = len(self.jobs)
            if total == 0:
                self.done.emit()
                return

            for idx, job in enumerate(self.jobs):
                if self._cancel.is_set():
                    job.status = "Cancelled"
                    self.job_update.emit(idx, job.status)
                    continue

                job.status = "Running"
                self.job_update.emit(idx, job.status)

                in_path = job.path
                in_dir = os.path.dirname(os.path.abspath(in_path))
                out_dir = in_dir if self.same_folder or not self.out_folder else self.out_folder
                job.out_dir = out_dir

                cmd = self._whisper_cmd(in_path, out_dir)
                self.log.emit(f"\n▶ Running: {' '.join(shlex.quote(c) for c in cmd)}\n")

                try:
                    self._proc = subprocess.Popen(
                        cmd,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        bufsize=1,
                        universal_newlines=True
                    )
                except Exception as e:
                    job.status = "Error"
                    job.error = str(e)
                    self.job_update.emit(idx, job.status)
                    self.log.emit(f"✖ Failed to start whisper: {e}\n")
                    continue

                # Coarse progress: just show job-based progress
                self.progress.emit(int((idx / total) * 100))

                # Stream output
                assert self._proc.stdout is not None
                for line in self._proc.stdout:
                    if self._cancel.is_set():
                        try:
                            self._proc.terminate()
                        except Exception:
                            pass
                        break
                    self.log.emit(line.rstrip("\n"))

                rc = self._proc.wait()
                if self._cancel.is_set():
                    job.status = "Cancelled"
                    self.job_update.emit(idx, job.status)
                    self.log.emit("⏹ Cancelled.\n")
                elif rc == 0:
                    job.status = "Done"
                    self.job_update.emit(idx, job.status)
                    self.log.emit("✓ Done.\n")
                else:
                    job.status = "Error"
                    job.error = f"whisper exited with code {rc}"
                    self.job_update.emit(idx, job.status)
                    self.log.emit(f"✖ Error: whisper exited with code {rc}\n")

                self.progress.emit(int(((idx + 1) / total) * 100))

            self.done.emit()

        except Exception as e:
            self.fatal.emit(str(e))


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"{APP_NAME} {APP_VERSION}")
        self.resize(980, 700)

        self.jobs: List[Job] = []
        self.thread: Optional[QtCore.QThread] = None
        self.worker: Optional[WhisperWorker] = None

        # ---- UI ----
        central = QWidget(self)
        root = QVBoxLayout(central)
        self.setCentralWidget(central)

        # Top controls
        controls = QHBoxLayout()

        self.model_box = QComboBox()
        self.model_box.setEditable(True)
        self.model_box.addItems(DEFAULT_MODELS)
        self.model_box.setCurrentText("turbo")

        self.format_box = QComboBox()
        self.format_box.addItems(OUTPUT_FORMATS)
        self.format_box.setCurrentText("txt")

        self.same_folder_chk = QCheckBox("Save next to input file")
        self.same_folder_chk.setChecked(True)

        self.out_folder_edit = QLineEdit()
        self.out_folder_edit.setPlaceholderText("Output folder (optional)")
        self.out_folder_btn = QPushButton("Choose…")
        self.out_folder_btn.clicked.connect(self.choose_output_folder)

        controls.addWidget(QLabel("Model:"))
        controls.addWidget(self.model_box, 1)
        controls.addSpacing(10)
        controls.addWidget(QLabel("Output:"))
        controls.addWidget(self.format_box)
        controls.addSpacing(10)
        controls.addWidget(self.same_folder_chk)
        controls.addSpacing(10)
        controls.addWidget(self.out_folder_edit, 2)
        controls.addWidget(self.out_folder_btn)
        root.addLayout(controls)

        # Extra args
        extras = QFormLayout()
        self.extra_args_edit = QLineEdit()
        self.extra_args_edit.setPlaceholderText('Optional: e.g., --language en --task transcribe --word_timestamps True')
        extras.addRow("Extra whisper args:", self.extra_args_edit)
        root.addLayout(extras)

        # Drop area + file list
        mid = QHBoxLayout()

        left = QVBoxLayout()
        self.drop = DropArea()
        self.drop.files_dropped.connect(self.add_paths)
        left.addWidget(self.drop)

        btns = QHBoxLayout()
        self.add_btn = QPushButton("Add Files…")
        self.add_btn.clicked.connect(self.add_files_dialog)
        self.remove_btn = QPushButton("Remove Selected")
        self.remove_btn.clicked.connect(self.remove_selected)
        self.clear_btn = QPushButton("Clear")
        self.clear_btn.clicked.connect(self.clear_jobs)
        btns.addWidget(self.add_btn)
        btns.addWidget(self.remove_btn)
        btns.addWidget(self.clear_btn)
        left.addLayout(btns)

        self.list = QListWidget()
        self.list.setSelectionMode(QtWidgets.QAbstractItemView.ExtendedSelection)
        left.addWidget(self.list, 1)

        mid.addLayout(left, 2)

        # Right panel: run controls + log
        right = QVBoxLayout()

        run_btns = QHBoxLayout()
        self.start_btn = QPushButton("Start")
        self.start_btn.clicked.connect(self.start)
        self.stop_btn = QPushButton("Stop")
        self.stop_btn.clicked.connect(self.stop)
        self.stop_btn.setEnabled(False)

        self.open_out_btn = QPushButton("Open Output Folder")
        self.open_out_btn.clicked.connect(self.open_output_folder)

        run_btns.addWidget(self.start_btn)
        run_btns.addWidget(self.stop_btn)
        run_btns.addWidget(self.open_out_btn)
        right.addLayout(run_btns)

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        right.addWidget(self.progress)

        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setPlaceholderText("whisper output will appear here…")
        right.addWidget(self.log, 1)

        mid.addLayout(right, 3)
        root.addLayout(mid, 1)

        # Status bar
        self.statusBar().showMessage("Ready")

        # Menu
        menubar = self.menuBar()
        help_menu = menubar.addMenu("&Help")

        act_check = QAction("Check for Updates…", self)
        act_check.triggered.connect(self.check_for_updates)
        help_menu.addAction(act_check)

        act_about = QAction("About", self)
        act_about.triggered.connect(self.about)
        help_menu.addAction(act_about)

        # Persist small settings
        self.settings = QtCore.QSettings("WhisperDrop", "WhisperDrop")
        self._restore_settings()

        # Disable output folder edit when "same folder" checked
        self.same_folder_chk.toggled.connect(self._sync_out_folder_enabled)
        self._sync_out_folder_enabled(self.same_folder_chk.isChecked())

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        self._save_settings()
        super().closeEvent(event)

    def _sync_out_folder_enabled(self, checked: bool):
        self.out_folder_edit.setEnabled(not checked)
        self.out_folder_btn.setEnabled(not checked)

    def _restore_settings(self):
        self.model_box.setCurrentText(self.settings.value("model", "turbo"))
        self.format_box.setCurrentText(self.settings.value("format", "txt"))
        self.same_folder_chk.setChecked(self.settings.value("same_folder", True, type=bool))
        self.out_folder_edit.setText(self.settings.value("out_folder", ""))
        self.extra_args_edit.setText(self.settings.value("extra_args", ""))

    def _save_settings(self):
        self.settings.setValue("model", self.model_box.currentText())
        self.settings.setValue("format", self.format_box.currentText())
        self.settings.setValue("same_folder", self.same_folder_chk.isChecked())
        self.settings.setValue("out_folder", self.out_folder_edit.text())
        self.settings.setValue("extra_args", self.extra_args_edit.text())

    # ---- File management ----
    def add_files_dialog(self):
        paths, _ = QFileDialog.getOpenFileNames(self, "Choose media files", "", MEDIA_FILTER)
        if paths:
            self.add_paths(paths)

    def add_paths(self, paths: List[str]):
        added = 0
        for p in paths:
            p = os.path.abspath(p)
            if os.path.isfile(p) and not any(j.path == p for j in self.jobs):
                self.jobs.append(Job(path=p))
                self.list.addItem(self._job_label(self.jobs[-1]))
                added += 1
        if added:
            self.statusBar().showMessage(f"Added {added} file(s).")
        else:
            self.statusBar().showMessage("No new files added.")

    def remove_selected(self):
        rows = sorted({i.row() for i in self.list.selectedIndexes()}, reverse=True)
        for r in rows:
            self.jobs.pop(r)
            self.list.takeItem(r)
        self.statusBar().showMessage(f"Removed {len(rows)} file(s).")

    def clear_jobs(self):
        if self.worker or self.thread:
            QMessageBox.information(self, "Busy", "Stop the current run before clearing.")
            return
        self.jobs.clear()
        self.list.clear()
        self.progress.setValue(0)
        self.log.clear()
        self.statusBar().showMessage("Cleared.")

    def _job_label(self, job: Job) -> str:
        base = os.path.basename(job.path)
        return f"[{job.status}] {base}  —  {job.path}"

    def _refresh_row(self, row: int):
        job = self.jobs[row]
        self.list.item(row).setText(self._job_label(job))

    # ---- Output folder ----
    def choose_output_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "Choose output folder")
        if folder:
            self.out_folder_edit.setText(folder)

    def open_output_folder(self):
        # Open either the selected output folder, or the folder of the first job
        if not self.jobs:
            return
        out = self.out_folder_edit.text().strip()
        if self.same_folder_chk.isChecked() or not out:
            out = os.path.dirname(os.path.abspath(self.jobs[0].path))
        QDesktopServices.openUrl(QUrl.fromLocalFile(out))

    # ---- Run control ----
    def start(self):
        if self.thread or self.worker:
            return
        if not self.jobs:
            QMessageBox.information(self, "No files", "Add at least one file to transcribe.")
            return

        # Reset statuses
        for i, j in enumerate(self.jobs):
            j.status = "Queued"
            j.error = None
            self._refresh_row(i)

        model = self.model_box.currentText().strip()
        out_format = self.format_box.currentText().strip()
        same_folder = self.same_folder_chk.isChecked()
        out_folder = self.out_folder_edit.text().strip()
        extra_args = self.extra_args_edit.text().strip()

        # Basic validation
        if not model:
            QMessageBox.warning(self, "Model missing", "Choose a model (e.g., turbo).")
            return
        if out_format not in OUTPUT_FORMATS:
            QMessageBox.warning(self, "Format invalid", f"Choose one of: {', '.join(OUTPUT_FORMATS)}")
            return
        if not same_folder and not out_folder:
            QMessageBox.warning(self, "Output folder missing", "Choose an output folder, or enable 'Save next to input file'.")
            return

        # UI state
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.add_btn.setEnabled(False)
        self.remove_btn.setEnabled(False)
        self.clear_btn.setEnabled(False)
        self.model_box.setEnabled(False)
        self.format_box.setEnabled(False)
        self.same_folder_chk.setEnabled(False)
        self.out_folder_edit.setEnabled(False)
        self.out_folder_btn.setEnabled(False)
        self.extra_args_edit.setEnabled(False)

        self.progress.setValue(0)
        self.statusBar().showMessage("Running…")

        # Thread + worker
        self.thread = QtCore.QThread(self)
        self.worker = WhisperWorker(self.jobs, model, out_format, same_folder, out_folder, extra_args)
        self.worker.moveToThread(self.thread)

        self.thread.started.connect(self.worker.run)
        self.worker.log.connect(self._append_log)
        self.worker.job_update.connect(self._on_job_update)
        self.worker.progress.connect(self.progress.setValue)
        self.worker.done.connect(self._on_done)
        self.worker.fatal.connect(self._on_fatal)

        self.thread.start()

    def stop(self):
        if self.worker:
            self.worker.cancel()
            self.statusBar().showMessage("Stopping…")

    def _append_log(self, line: str):
        self.log.appendPlainText(line)

    def _on_job_update(self, row: int, new_status: str):
        if 0 <= row < len(self.jobs):
            self.jobs[row].status = new_status
            self._refresh_row(row)

    def _teardown(self):
        if self.thread:
            self.thread.quit()
            self.thread.wait(2000)
        self.thread = None
        self.worker = None

        # UI state
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.add_btn.setEnabled(True)
        self.remove_btn.setEnabled(True)
        self.clear_btn.setEnabled(True)
        self.model_box.setEnabled(True)
        self.format_box.setEnabled(True)
        self.same_folder_chk.setEnabled(True)
        self._sync_out_folder_enabled(self.same_folder_chk.isChecked())
        self.extra_args_edit.setEnabled(True)

    def _on_done(self):
        self._append_log("\nAll jobs finished.\n")
        self.statusBar().showMessage("Done.")
        self.progress.setValue(100)
        self._teardown()

    def _on_fatal(self, msg: str):
        QMessageBox.critical(self, "Error", msg)
        self.statusBar().showMessage("Error.")
        self._teardown()

    # ---- Help / Updates ----
    def about(self):
        QMessageBox.information(
            self,
            "About WhisperDrop",
            f"{APP_NAME} {APP_VERSION}\n\n"
            "A drag-and-drop GUI wrapper for the `whisper` CLI (openai/whisper).\n"
            "It runs transcription locally using your installed Whisper setup."
        )

    def check_for_updates(self):
        """
        Two update approaches:
        1) GitHub releases (recommended for a packaged build).
        2) pip upgrade (if you installed WhisperDrop as a pip package).
        """
        if not GITHUB_REPO and not PIP_PACKAGE:
            QMessageBox.information(
                self, "Updates not configured",
                "Update checks are not configured yet.\n\n"
                "If you publish this app, set either:\n"
                "  • GITHUB_REPO = \"youruser/yourrepo\" (GitHub Releases)\n"
                "or\n"
                "  • PIP_PACKAGE = \"your-package-name\" (pip upgrades)\n"
                "in the source code.\n\n"
                "For now, you can just pull the latest source and rerun."
            )
            return

        # Prefer GitHub Releases if configured
        if GITHUB_REPO:
            self._check_updates_github()
        elif PIP_PACKAGE:
            self._check_updates_pip()

    def _check_updates_github(self):
        import json
        import urllib.request

        api = f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest"
        try:
            req = urllib.request.Request(api, headers={"User-Agent": f"{APP_NAME}/{APP_VERSION}"})
            with urllib.request.urlopen(req, timeout=10) as resp:
                data = json.loads(resp.read().decode("utf-8", errors="replace"))

            tag = (data.get("tag_name") or "").lstrip("v")
            html_url = data.get("html_url") or f"https://github.com/{GITHUB_REPO}/releases"
            if not tag:
                raise RuntimeError("No tag_name found in GitHub response.")

            if self._is_newer(tag, APP_VERSION):
                res = QMessageBox.question(
                    self,
                    "Update available",
                    f"A newer version is available: {tag}\n\n"
                    "Open the download page?",
                    QMessageBox.Yes | QMessageBox.No
                )
                if res == QMessageBox.Yes:
                    QDesktopServices.openUrl(QUrl(html_url))
            else:
                QMessageBox.information(self, "Up to date", f"You’re on the latest version ({APP_VERSION}).")
        except Exception as e:
            QMessageBox.warning(self, "Update check failed", f"Could not check updates.\n\n{e}")

    def _check_updates_pip(self):
        # Minimal pip approach: attempt to upgrade in-app.
        res = QMessageBox.question(
            self,
            "Update via pip",
            "This will run pip to upgrade the app in the current Python environment.\n\n"
            "Proceed?",
            QMessageBox.Yes | QMessageBox.No
        )
        if res != QMessageBox.Yes:
            return
        try:
            cmd = [sys.executable, "-m", "pip", "install", "-U", PIP_PACKAGE]
            self._append_log(f"\n▶ Running: {' '.join(shlex.quote(c) for c in cmd)}\n")
            p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            out, _ = p.communicate()
            self._append_log(out)
            if p.returncode == 0:
                QMessageBox.information(self, "Updated", "Update complete. Restart the app.")
            else:
                QMessageBox.warning(self, "Update failed", f"pip exited with code {p.returncode}. See log.")
        except Exception as e:
            QMessageBox.warning(self, "Update failed", str(e))

    @staticmethod
    def _is_newer(latest: str, current: str) -> bool:
        def parse(v: str) -> Tuple[int, int, int]:
            parts = [int(p) if p.isdigit() else 0 for p in v.split(".")]
            parts += [0] * (3 - len(parts))
            return parts[0], parts[1], parts[2]
        return parse(latest) > parse(current)


def main():
    app = QApplication(sys.argv)
    # Better default font on HiDPI
    app.setApplicationName(APP_NAME)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
