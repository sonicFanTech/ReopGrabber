# Repo Grabber

**ReopGrabber** is a lightweight Windows console utility that monitors public GitHub repositories and downloads updated repository snapshots automatically.

Add a repository once, choose where its files should be stored, and let ReopGrabber check for changes in the background.

## Features

- Add public GitHub repositories using their URL.
- Save your repository list beside the program as `repositories.txt`.
- Check the latest commit on each repository's default branch.
- Download updated repositories as ZIP archives.
- Extract updates into a folder you choose.
- Stage new files before replacing an existing downloaded copy.
- Ask before downloading updates, or enable automatic downloads.
- Configure the update-check interval.
- Run monitoring when Windows starts.
- Use a simple, color-coded console interface.

## Getting Started

1. Download or build ReopGrabber.
2. Run `ReopGrabber.exe`.
3. Select **Add GitHub repository**.
4. Paste a public GitHub repository URL, such as `https://github.com/owner/repository`.
5. Enter the full folder path where the repository should be extracted.
6. Use **Check all repositories now** or start background monitoring.

ReopGrabber creates its configuration files next to the executable:

- `repositories.txt` — saved repository URLs, destination folders, and installed commit markers.
- `settings.ini` — monitoring and startup preferences.

## Settings

You can configure the check interval, choose whether ReopGrabber asks before downloading an update, and enable launching the monitor when you sign in to Windows.

When approval is enabled, ReopGrabber needs to remain accessible so you can respond to download prompts.

## Building from Source

ReopGrabber is a native C++ Windows console application.

**Build requirements:**
- Visual Studio 2022
- **Desktop development with C++** workload
- MSVC v143 toolset
- Windows 10 SDK or newer

To build:

1. Open `ReopGrabber.sln` in Visual Studio.
2. Select **Release** and **x64**.
3. Build the solution.

The project uses C++17 and does not require CMake.

## Compatibility and Notes

- Designed for Windows 10 and Windows 11.
- The current version supports public GitHub repositories; private repositories are not supported.
- Update checks follow the repository's default branch and use its latest commit as the update marker.
- ReopGrabber downloads source ZIP snapshots. It does not install or build the downloaded projects for you.
- Keep ReopGrabber in a folder where it can write its configuration files.

## License

ReopGrabber is free and open-source software licensed under the **GNU General Public License v3.0 (GPL-3.0)**. See the [LICENSE](LICENSE) file for the full license text.

---

Copyright (C) 2026 **sonic Fan Tech**.
