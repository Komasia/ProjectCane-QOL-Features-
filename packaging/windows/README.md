# Windows installer

`ProjectCane-Setup.nsi` builds a single-file Windows installer
(`ProjectCane-QOL-v<version>-Setup.exe`). The installer contains **no game
data**. When it runs, it asks for the user's own NTSC (US) Sly Cooper and the
Thievius Raccoonus disc image (`SCUS-97198`, `.iso`) and extracts the worlds
and cutscenes from it with the File Extractor.

## What the installer does

- Installs per user to `%LOCALAPPDATA%\Programs\ProjectCane`, with no
  administrator rights needed.
- Installs:
  - the game, the FFmpeg / Streamline / DLSS / FidelityFX runtimes and the
    fonts;
  - the File Extractor;
  - the docs and third-party licenses (in `Docs\`).
- Shows an ISO page:
  - a Browse button;
  - a size check;
  - a **Skip** option for reinstalls that keep the existing game files (game
    files from 0.4 and older are extracted again, because 0.5 can't load them).
- Runs `File Extractor.exe "<iso>" --no-pause` and shows its output in the
  install log. If extraction fails, the user can pick another file or finish
  and extract later.
- Adds Start menu entries for the game, for "Re-extract game files" and for
  the uninstaller, plus an optional desktop shortcut.
- Never installs settings, saves or save states, and leaves out the RTX 20/30
  Frame Generation mod (`version.dll`).
- Has an uninstaller that removes the extracted data and asks before deleting
  saves and settings.

## Building

1. In Visual Studio, open `ProjectCane.sln` and build **Release | x64** for:
   - **Sly1-Release**, which outputs to `x64\Release\` (the exe, runtime DLLs
     and `Fonts\`);
   - **File Extractor**, which outputs to `x64\Extractor\File Extractor.exe`.
     The Release build uses the static C runtime, so it runs on PCs without the
     Visual C++ redistributable.
2. Install [NSIS](https://nsis.sourceforge.io/) 3.x and run:

   ```
   makensis packaging\windows\ProjectCane-Setup.nsi
   ```

   The installer is written to this folder.

You can override these with `-D`:

| Define | Default | Purpose |
| --- | --- | --- |
| `SRC` | `../../x64/Release` | Folder that holds the built game exe, DLLs and `Fonts\` |
| `VERSION` | `0.5.0-beta` | Version shown in Apps & Features and used in the file name |
| `OUTDIR` | `.` | Where the installer is written |

For example: `makensis -DSRC=../../x64/Debug -DVERSION=0.5.1 packaging\windows\ProjectCane-Setup.nsi`

## File Extractor command line

```
"File Extractor.exe" <path-to-iso> [--no-pause]
```

The extractor writes `Worlds\`, `Cutscenes\` and `extraction_manifest.json`
next to its own exe. Levels are converted to ProjectCane's PCBRX format with the
level rebuilder from `File Extractor GUI\NativeTools\BrxRebuilder`, which is
compiled into the extractor.
`--no-pause` skips the "Press Enter to close" prompt.
