# Main_MiSTer with `.showlist` / `.hidelist` / `.nomedia` support

This is a fork of [MiSTer-devel/Main_MiSTer](https://github.com/MiSTer-devel/Main_MiSTer)
(the main binary of the [MiSTer FPGA](https://github.com/MiSTer-devel/Wiki_MiSTer/wiki)
project) that adds three optional, per-directory files — **`.showlist`**,
**`.hidelist`** and **`.nomedia`** — giving fine-grained control over which
files and folders appear in the OSD file browser.

The features were proposed upstream in
[MiSTer-devel/Main_MiSTer#443](https://github.com/MiSTer-devel/Main_MiSTer/issues/443);
the `.nomedia` marker follows the suggestion made by the upstream maintainer
in that discussion.

## Motivation

Directories on the SD card often accumulate entries that you never select
from the OSD: BIOS images, alternate ROM revisions, artwork, work files, or
simply cores and folders you rarely use. The stock firmware shows all of
them. With this fork you can curate what the OSD displays — per directory,
without moving, renaming, or deleting anything on the card.

## How it works

Whenever the OSD scans a directory, it additionally looks for three optional
files *in that same directory*:

| File | Effect |
| --- | --- |
| `.showlist` | Whitelist — if present, only entries that match at least one of its patterns are shown. |
| `.hidelist` | Blacklist — entries that match any of its patterns are hidden, even if they also match `.showlist`. |
| `.nomedia` | Marker (Android-style) — if the file merely exists, all regular files in the directory are hidden; subfolders remain visible. Its content is ignored, so an empty file suffices. |

Further notes on the semantics:

- Filtering applies to both **files and subfolders** of the directory
  containing the list file(s).
- It works in every directory the OSD browser scans, including the
  top-level of the SD card (`/media/fat`) — so, for example, a `.hidelist`
  there can hide entire sections such as `_Utility` from the main menu.
- The parent-directory entry (`..`) is never filtered, so you can always
  navigate back up.
- `.nomedia` is evaluated first: files it hides cannot be brought back by a
  `.showlist`. Subfolders remain subject to both lists, so `.nomedia` and a
  `.hidelist` (for folders) can be combined. Since detecting the marker is a
  single file-existence check, it adds virtually no processing cost.
- If none of these files are present, behaviour is identical to stock
  firmware.
- The files are re-read on every directory scan, so changes (e.g. made over
  SSH or Samba) take effect the next time the directory is opened — no
  reboot required.

## Pattern format

Both files use the same format: **one pattern per line**, where each pattern
must match the *entire* on-disk name of a file or folder.

- By default, every character is **literal**. Characters that are special in
  regular expressions (`. ( ) [ ] { } * + ? ^ $ | \`) are matched verbatim,
  so a line like

  ```
  Sonic The Hedgehog (USA, Europe).md
  ```

  matches exactly that file — and nothing else.
- Text enclosed in **backticks** (`` ` ``) is inserted as raw
  [ECMAScript regex](https://en.cppreference.com/w/cpp/regex/ecmascript)
  syntax. Literal and regex parts can be mixed freely within one line:

  | Line | Matches |
  | --- | --- |
  | `` `.*\.mra` `` | every name ending in `.mra` |
  | ``Super Mario `.*` `` | every name starting with `Super Mario ` |
  | ``Street Fighter II`('\|f)`.mra`` | `Street Fighter II'.mra` and `Street Fighter IIf.mra` |

- Matching is **case-sensitive** and against the real directory entry name —
  not against the prettified name shown in the OSD (aliases from
  `names.txt`, stripped extensions, etc. are applied *after* filtering).
- A name must match a pattern **completely**; `Sonic` does *not* match
  `Sonic.md`. Use e.g. ``Sonic`.*` `` to match by prefix.
- Leading and trailing whitespace is trimmed (Windows/DOS line endings are
  handled), empty lines are skipped, and a line that results in an invalid
  regular expression is silently ignored. Lines are limited to 4095
  characters.

## Examples

Show only a hand-picked set of games in `games/Genesis/`
(`/media/fat/games/Genesis/.showlist`):

```
Sonic The Hedgehog (USA, Europe).md
Streets of Rage 2 (USA).md
`Micro Machines.*`
```

Hide BIOS files and a work folder anywhere (`.hidelist` in that directory):

```
`.*[Bb][Ii][Oo][Ss].*`
wip
```

Hide the utility and console sections from the main menu
(`/media/fat/.hidelist`):

```
_Utility
_Console
```

Hide the (auto-updated) MRA files in `_Arcade` while keeping your own
curated subfolders visible — the original use case from
[#443](https://github.com/MiSTer-devel/Main_MiSTer/issues/443):

```
ssh root@<mister-ip> 'touch /media/fat/_Arcade/.nomedia'
```

Since the OSD browser itself hides dot-files, create and edit these lists
from a PC (card reader), over Samba, or via SSH, e.g.:

```
ssh root@<mister-ip> 'cat > /media/fat/games/Genesis/.showlist' <<'EOF'
Sonic The Hedgehog (USA, Europe).md
EOF
```

## Limitations

- Filtering does **not** apply *inside* ZIP archives browsed as folders
  (the ZIP file itself, being a regular directory entry, can be hidden).
- ZIP archives count as regular files, so `.nomedia` hides them too, even
  though the OSD would otherwise present them like folders.
- `.nomedia` does not hide folders; use a `.hidelist` for that.
- Names are matched byte-wise; no Unicode case folding.

## Building

The build is unchanged from upstream — see the
[official cross-compilation instructions](https://mister-devel.github.io/MkDocs_MiSTer/developer/mistercompile/).
This fork additionally lets you point the build at a toolchain that is not
on your `PATH`:

```
make TOOLCHAIN=toolchain/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin
```

Alternatively, build with Docker using the repository's devcontainer image
(this is how this fork is compile-tested):

```
docker build -t mister-toolchain .devcontainer
docker run --rm -v "$PWD":/src -w /src mister-toolchain \
    bash -c 'PATH="$PATH:$(echo /usr/local/bin/gcc-arm-*/bin)" make'
```

The resulting `bin/MiSTer` replaces `/media/fat/MiSTer` on the SD card
(kill the running `MiSTer` process or reboot afterwards).

## Relation to upstream

The fork consists of a small, self-contained patch to `file_io.cpp` (plus
the `Makefile` tweak above) and is rebased onto upstream master
(currently commit `7317947`, 2026-07-13). The original upstream README,
with pointers to the MiSTer wiki, is
[here](https://github.com/MiSTer-devel/Main_MiSTer#readme).

Like upstream, this fork is licensed under the [GPLv3](LICENSE).
