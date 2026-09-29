# SeaDrive Dolphin extension

Two KIO plugins for Dolphin (KDE):

- `seadrive-dolphin-overlay`: sync/lock/cloud status emblems on files and folders
  in `~/SeaDrive`.
- `seadrive-dolphin-actions`: the "SeaDrive" right-click menu (lock/unlock,
  download/remove cache, share/internal/upload links, file history, and the
  SeaDrive menu on empty space in `~/SeaDrive`).

## Build and install

Plasma 6 (KF6):
```
# Debian/Ubuntu: sudo apt install cmake g++ extra-cmake-modules libkf6kio-dev qt6-base-dev
# Fedora:        sudo dnf install cmake gcc-c++ extra-cmake-modules kf6-kio-devel qt6-qtbase-devel
# Arch:          sudo pacman -S cmake gcc extra-cmake-modules kio qt6-base
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Plasma 5 (KF5), e.g. Kubuntu 24.04:
```
sudo apt install cmake g++ extra-cmake-modules libkf5kio-dev qtbase5-dev
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_WITH_QT6=OFF
cmake --build build
sudo cmake --install build
```

Then restart Dolphin (close all windows). The menu can be toggled in
Dolphin > Configure > Context Menu.

## Dolphin downloads files

SeaDrive downloads a file as soon as a program opens it. Dolphin opens files
in `~/SeaDrive` in two cases:

- Previews (thumbnails) read file contents. Folder previews also read files
  inside subfolders you never opened.
- Right-clicking a file makes KIO detect its exact file type by reading its
  first bytes (`KFileItem::determineMimeType()`). This runs before any
  context menu plugin, so this extension cannot prevent it.

Both happen because KIO 6.30 and older (and all of KF5) treat the SeaDrive
FUSE mount as a local disk. KIO master treats `fuse.*` mounts as remote
("slow"): the type is then guessed from the file extension only, and the
default settings skip previews on remote filesystems. The changes are
[741f40da](https://invent.kde.org/frameworks/kio/-/commit/741f40da195022b5d426b5459734eae42ec4df5e)
and [0d477be2](https://invent.kde.org/frameworks/kio/-/commit/0d477be2e668039a022b05199da9367b32849edb),
expected in KIO 6.31 (KDE Frameworks 6.31). Check your version with your
package manager, e.g. `pacman -Q kio`.

Nothing helps with the right-click download before that release; use the
SeaDrive app for lock/share/history on files that are not downloaded yet.

To stop preview downloads until then, turn previews off for `~/SeaDrive` only:

1. Dolphin > Configure > General (or Display Style): select
   "Remember display style for each folder".
2. Open `~/SeaDrive`, then View > Adjust View Display Style…
3. Clear "Show previews", set "Apply to" to
   "Current folder including all subfolders", click OK.

Folders created later do not inherit this; repeat step 3 when needed.
Alternatively, clear "Folders" in Dolphin > Configure > Interface > Previews.
This stops folder-preview downloads, but for all folders on the system.

To see which process opens files: `sudo opensnoop | grep SeaDrive`
(package `bcc-libbpf-tools` on Arch).

## Test

`test/` loads the built plugins the way Dolphin does, against a fake SeaDrive socket:
```
DESTDIR=$PWD/root cmake --install build
cmake -S test -B test/build -DQTV=6 && cmake --build test/build
bash test/run.sh test/build "$(find $PWD/root -type d -name plugins)" 6
```
