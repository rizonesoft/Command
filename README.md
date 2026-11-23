# Command

A family of small console tools for Windows and, later, Impossible OS, built on Impossible SDK. Each tool is one executable with no C runtime that calls only the tier 0 Windows functions. Status: first version of five tools.

| Tool | What it does |
| --- | --- |
| `thash` | SHA-256, SHA-1, MD5, CRC-32 and BLAKE3 of files and standard input in one pass, and a check mode |
| `thex` | Hex dump with offset and length, and the reverse with an exact round trip |
| `tfind` | Find fixed text in files and folders, with line numbers |
| `ttree` | Folder tree with sizes and totals |
| `tcmp` | Compare two files or two folders |

Every tool takes `--help` and `--version`. Exit codes are the same everywhere: 0 success, 1 a negative answer (nothing found, files differ, a checksum did not match), 2 an error. An error is one line on standard error that ends with the Windows error number, for example `thash: x.bin: The system cannot find the file specified. (error 2)`. A read error halfway through a file is an error and exit status 2: no hash or "identical" is reported for it. `tfind` has already printed the matches it read before the error; it then reports the error.

## Build and test

Clone Impossible SDK beside this folder at the commit in `sdk.json`, then run this in Windows or WSL:

```
python ../ImpossibleSDK/tools/app_build.py .
```

The build runs the tests in `tests/` (Python `unittest`; they need Python 3.12 or newer, as the SDK build does). They run the programs directly on Windows or through WSL interop, and create their scratch folders under the build folder.

## thash

```
thash [-a LIST] [--tag] [FILE]...
thash -c LIST [-a blake3] [-q]
```

With no FILE, or `-`, it reads standard input. `-a` takes a comma separated list of `sha256` (the default), `sha1`, `md5`, `crc32`, `blake3` or `all`; the data is read once however many are chosen. One algorithm prints `hash  name`; several, or `--tag`, print `SHA256 (name) = hash`.

`-c LIST` reads such lines from the file LIST (`-` for standard input) and prints `name: OK`, `name: FAILED` or `name: FAILED open or read` for each. A plain 64 digit hash is read as SHA-256, or as BLAKE3 with `-a blake3`; the other lengths identify themselves. Names are relative to the current folder. `-q` prints only failures. Exit 0 all good, 1 a mismatch, 2 an unreadable file or a line that is not a checksum (a line with a NUL byte is not one). Standard input named in the list is read once, for every algorithm at the first record that names it. Standard input can be named only once on the command line (`thash - -` is an error). In every tool, a regular file named on the command line whose size changes while it is read is an error: the reader checks the size on every read and the byte count at the end. Standard input is not checked, because with tier 0 calls alone a pipe cannot be told from a redirected file (both answer `GetFileSizeEx` and `SetFilePointerEx` alike).

## thex

```
thex [-s OFFSET] [-n LENGTH] [-w BYTES] [FILE]
thex -r [-o OUTPUT] [FILE]
```

The dump is a hex offset (at least 8 digits), the bytes in groups of eight, and the text between bars:

```
00000000  48 65 6c 6c 6f 20 77 6f  72 6c 64 0a 00 01 02 03  |Hello world.....|
```

Numbers may be decimal, `0x` hex, or end in `k`, `m`, `g` or `t` (powers of 1024). `-r` turns a dump back into the exact bytes: every line's offset must follow on from the line before (a dump of a range made with `-s` is fine), and the text column is ignored. With `-o`, the output is written to a new file named `.thex-<unique>` (a short name in the same folder, created with CREATE_NEW, so an existing file is never touched, and only deleted by the run that made it) and renamed over `OUTPUT` only when the whole dump has been read and written, so a bad dump, an error or Ctrl+C never leaves a half written `OUTPUT` (Ctrl+C can leave that temporary file).

## tfind

```
tfind [-i] [-N] [-l] [-q] TEXT [PATH]...
```

Prints `path:line:text` for each line that contains TEXT. A folder PATH is searched in full in sorted order; symbolic links and junctions, inside it or named as a PATH, are not entered. With no PATH, standard input is searched. `-i` folds A to Z only, `-N` leaves out line numbers, `-l` lists file names, `-q` prints nothing and stops at the first match. Exit 0 found, 1 not found, 2 error (even when something matched).

A file with a NUL byte in its first 64 KiB is binary: a match prints `path: binary file matches` and no bytes. A matching line that holds a NUL later in a text file is reported the same way. UTF-16 files (with a byte order mark) are not searched and are reported as an error. Lines may be up to 64 MiB.

## ttree

```
ttree [-d DEPTH] [-b] [-D] [--ascii] [PATH]...
```

Prints the tree with the size of every file and folder (a folder's size is everything inside it) and the folder count, file count and total at the end. `-d` limits the levels listed but the totals still cover everything; `-b` gives exact bytes; `-D` lists folders only. Only real symbolic links and junctions count as links (other reparse points, such as deduplicated or cloud files, are ordinary files with their real size). A link is marked `[symlink]` or `[junction]` and is neither entered nor counted, also when it is the PATH; a link to a file is listed but not counted as a file. A folder that cannot be read is marked `[error N]`, reported, and the exit status is 2. The whole listing is held in memory until the totals are known.

## tcmp

```
tcmp [-a] [-v] [-q] FIRST SECOND
```

Two files: reports the first differing byte (`differ at offset 2 (0x2): 0x63 vs 0x58`) or the size difference, and with `-a` every run of differing bytes. Two folders: lists what is only in one, what is different (by size, then by every byte) and, with `-v`, what is the same, then a one line total. Names match as Windows matches them (A to Z fold). Folder links (also when given as FIRST or SECOND) are listed as skipped, and a comparison with anything skipped is not "identical" (exit 1). Exit 0 identical, 1 different, 2 error.

## Arguments, paths and output

The command line is split with the usual Windows rules, including backslash-quote cases and empty arguments. Names are Unicode. Absolute paths of 240 characters or more are handed to Windows with the `\\?\` prefix, so deep trees work; relative paths have Windows' usual 260 character limit, because there is no tier 0 call that gives the current folder. Standard output is UTF-8 when redirected and console text otherwise. Folder walks keep their state on the heap, so depth is limited by memory, not the stack. Files of any size are read in fixed 1 MiB buffers.

Ctrl+C ends a tool at once (no handler is installed, because `SetConsoleCtrlHandler` is outside tier 0); open files close with the process, and the only file a tool writes, by `thex -r -o`, is replaced only on success.

## Layout

`common/` has the shared code: text conversion, output, command line and option parsing, files and folder listings, line reading. `common/hash/` has the five hash functions as plain integer C with no Windows calls, so they can be reused. `src/` has one file per tool, plus `targs.c`, a helper the tests use to check the argument parser.

## License

GPL-3.0-only. See [LICENSE](LICENSE).
