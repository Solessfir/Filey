<h1 align="center"><img src="Source/Resources/Filey.svg" width="40" alt=""> Filey</h1>

<p align="center">A fast, keyboard-first file manager for Windows and Linux, written in C.</p>

> [!IMPORTANT]
> Filey is heavily inspired by [File Pilot](https://filepilot.tech/) and is a vibe-coded project.
> If you care about hand-crafted software, use File Pilot instead, or better, buy it.
> Developers like Vjekoslav Krajačić, who build amazing apps like it by hand, deserve our support.

![Filey with two panels: a git repository on the left and an archive listed in the inspector on the right](Docs/Screenshot.png)

## Features

- **Panels and tabs**: split panels side by side or stacked, tabs that move between panels and windows, and layouts you can save and restore.
- **Fast lists**: details, list, columns and icon views with thumbnails, folder sizes, filters as you type, an expanded search through subfolders, Miller columns, a tree view that unfolds folders in place, one at a time or with everything inside, and items grouped under headings by name, type, date, size, item count or share of the folder or drive, and in the bin and This PC by what only they show.
- **Go To and the command palette**: jump to recent, bookmarked or typed paths, and run any command by name. Every hotkey can be changed.
- **Inspector**: previews text with syntax highlighting and line numbers, images, PDFs, 3D models (OBJ, STL, PLY, glTF, FBX, .blend, USD, Collada, 3DS, 3MF), see-through where their materials are, Word and Excel text, archives, and any file as hex; a folder or drive can show by what takes its space instead, as a treemap of everything in it with its folders by size and its kinds of file, whose items can be trashed, deleted or shown in their folder.
- **Archives as folders**: browse zip, 7z, rar, tar and cab files, archives inside archives too, and drag, copy and paste in and out of them. An encrypted archive asks for its password; encrypted 7z and RAR ones open with [7-Zip](https://www.7-zip.org) installed.
- **Network**: the computers on your network that share folders, under Network on Windows, and their shares. On Linux, **Connect to Server** opens `sftp://`, `smb://`, `ftp://` and WebDAV addresses through GVFS, asking for a password when the server wants one, and lists them under Storage until you disconnect.
- **Phones and cameras**: browse a phone or camera connected by USB, under This PC on Windows and through GVFS on Linux (with `gvfs-mtp`), and copy, move, rename and delete on it as in any folder, which updates as the phone changes. On Linux, Filey mounts them as they connect, and Eject in the right-click menu takes them and USB drives away. Partitions nothing has mounted, such as a Windows one, are listed under Storage too and mounted when opened, as Nemo does.
- **Git**: the branch, the commits to push and pull, the changed files colored in the list, branch switching and a safe pull that only fast-forwards.
- **Themes**: Catppuccin, Nord, Gruvbox, Tokyo Night, One Dark and Filey Dark built in, schemes of your own, starting from the system's light or dark mode until you pick one, and a transparent glass look in any of them.
- **Linux desktop**: the file manager other programs open for Show in Folder, New from your Templates folder, Set as Default in Open With, your Nemo actions and scripts in the right-click menu, and Open as Root.
- **Languages**: English, Chinese, French, German, Polish, Portuguese, Russian and Spanish, plus your own through a translation file.

## Optional tools

Filey uses these when they are on your `PATH`. Without them, their features just stay hidden.

| Tool | What it adds |
| --- | --- |
| [FFmpeg](https://ffmpeg.org/) (`ffmpeg`, `ffprobe`) | Audio waveforms you can play and scrub in the inspector, video frames, media details, and **Compress Video**, which encodes on the graphics card (NVENC, AMF or Quick Sync) when it can. |
| [yt-dlp](https://github.com/yt-dlp/yt-dlp) | Paste a video's web address into a folder to download it there, as MP4 or audio only, alone or with its whole playlist. |
| [aria2](https://aria2.github.io/) (`aria2c`) | Download torrents: paste a magnet link, tick the files you want in the inspector and press Download. Five download at once and the rest wait their turn; downloads left unfinished go on the next time Filey starts. |
| [Git](https://git-scm.com/) | The Git features above. It never asks for a login: a repository that needs one is left alone. |

## Install

### Windows

Download `Filey-<version>-windows-x64.zip` from [Releases](https://github.com/Solessfir/Filey/releases), unzip it and run `Filey.exe`.
**Filey Setup** in the main menu installs it for your user, with shortcuts, the `PATH` entry, and the option to make it your default file manager.

### Linux

Installs the latest release for your user (`~/.local/bin`, with a desktop entry and icons), and makes it the file manager other programs open for Show in Folder. It needs Wayland, `freetype2` and `fontconfig`, and `libarchive` for archives.

```sh
curl -sSL https://raw.githubusercontent.com/Solessfir/Filey/main/install.sh | bash -s install
```

To uninstall it (your settings in `~/.config/filey` stay):

```sh
curl -sSL https://raw.githubusercontent.com/Solessfir/Filey/main/install.sh | bash -s uninstall
```

`wget -qO-` works in place of `curl -sSL` in both.

### Command line

`filey <folder or file>...` opens each in a new tab of the open window, a file in its folder with it selected. `--replace` shows the first in the current tab instead, `--split` (or `--split=bottom`) opens each in a new panel beside the current one, `--inspect` opens the inspector on a file given, and `--new-window` opens a window of its own.

### Updates

Filey asks GitHub once a day whether a newer release is out (with `curl`, which Windows 10 and later and Linux desktops have). When one is, a notice and the main menu offer **Update**, which downloads it, puts it in place of the running Filey and starts it again with your panels and tabs. **Update Check** in the Options turns the daily check off; **Check For Updates** in the command palette asks at once.

### Colors from your wallpaper

Filey takes its colors from your wallpaper through [Matugen](https://github.com/InioX/matugen). In Options, duplicate any color scheme: the copy is a custom scheme, `Custom1.scheme` in `~/.config/filey`, and is chosen at once. Get the template:

```sh
mkdir -p ~/.config/matugen/templates
curl -sSL https://raw.githubusercontent.com/Solessfir/Filey/main/Source/Resources/filey-matugen.conf -o ~/.config/matugen/templates/filey.conf
```

Then point Matugen at that scheme in `~/.config/matugen/config.toml`:

```toml
[templates.filey]
input_path = '~/.config/matugen/templates/filey.conf'
output_path = '~/.config/filey/Custom1.scheme'
```

Each time Matugen runs, Filey takes the new colors at once.

## Build

Filey builds with Clang 19 or later (`-std=c23`).

- **Windows**: install Visual Studio Build Tools with the C++ Clang tools, then run `Build.bat release`.
- **Linux**: install `clang`, `pkg-config` and the development packages of `wayland`, `libxkbcommon`, EGL, `freetype2` and `fontconfig`, then run `./Build.sh release`. `./Build.sh release install` installs your build for your user.

Add `test` to either command to run the tests. The build goes to `Bin/release`.

## License

Licensed under the [MIT License](LICENSE).
