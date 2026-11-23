---
schema_version: 1
id: text-and-file-tools
domain: 02-text-and-files
status: active
title: "TODO-01 -- Text and File Tools"
---

# TODO-01 -- Text and File Tools

> **Goal:** The Windows text and file commands, safe with large files and links.

> **Current state:** Measured 2026-11-23. The shared reader, link classifier and hashes exist in `common/`. None of these tools is built.

## Inputs

- `docs/tools.md`: which tools, and their order.
- `common/`: the shared reader and classifier.


## Outcome

- Eleven tools behave as documented and are compared with Windows' own.
- None follows a link out of where it was told to work.


**Adjacency:** all=not-applicable (commands keep no records, settings or reports)

**Adjacency rationale:** Each command prints and exits.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | find and findstr | -- | [ ] |
| 2 | §2 | sort, more and clip | -- | [ ] |
| 3 | §3 | fc, comp and where | -- | [ ] |
| 4 | §4 | tree, attrib and xcopy | -- | [ ] |
| 5 | §5 | robocopy | §4 | [ ] |

## 1. find and findstr

Literal and simple pattern search with the documented switches, on the shared reader.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/find.c` and `src/findstr.c`. Done when: each tool's documented cases pass.
2. Compare with Windows' own tool. Done when: the comparison test passes.

- [ ] Match literals and findstr's own small pattern language in linear time in `common/match.c`. Done when: 5,000 random cases equal Windows' output. Cheaper substitute: literals only.
- [ ] Handle the two tools' switches and exit codes. Done when: every documented switch has a case. Cheaper substitute: the common ones.
- [ ] State each place where findstr's own behaviour is a known fault and what is done instead. Done when: `docs/findstr.md` lists them with tests. Cheaper substitute: silence.
- [ ] Commit: `command: find and findstr`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## 2. sort, more and clip

Sorting by column and locale rules, paging and the clipboard.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/sort.c`, `src/more.c` and `src/clip.c`. Done when: each tool's documented cases pass.
2. Compare with Windows' own tool. Done when: the comparison test passes.

- [ ] Sort files larger than memory in `src/sort.c` with bounded runs. Done when: a 4 GB sparse text sorts inside 256 MB. Cheaper substitute: in memory.
- [ ] Page in `src/more.c` and pass through when redirected. Done when: a redirected run gives the same bytes. Cheaper substitute: always paging.
- [ ] Send text in `src/clip.c` with the right encoding. Done when: text with emoji comes back equal. Cheaper substitute: the local code page.
- [ ] Commit: `command: sort, more and clip`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## 3. fc, comp and where

Comparing files as text and bytes, and finding programs on the path.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/fc.c`, `src/comp.c` and `src/where.c`. Done when: each tool's documented cases pass.
2. Compare with Windows' own tool. Done when: the comparison test passes.

- [ ] Compare text with resynchronising in `src/fc.c`, bounded in memory. Done when: cases equal Windows' tool. Cheaper substitute: first difference only.
- [ ] Compare bytes in `src/comp.c` on the shared reader. Done when: the size table of `tcmp` passes here too. Cheaper substitute: equal sizes only.
- [ ] Search the path and patterns in `src/where.c`. Done when: results equal Windows' tool in order. Cheaper substitute: the first hit.
- [ ] Commit: `command: fc, comp and where`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## 4. tree, attrib and xcopy

The folder drawing, file attributes and copying folders.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/tree.c`, `src/attrib.c` and `src/xcopy.c`. Done when: each tool's documented cases pass.
2. Compare with Windows' own tool. Done when: the comparison test passes.

- [ ] Draw folders in `src/tree.c` with links marked. Done when: output equals Windows' tool for the fixture. Cheaper substitute: names only.
- [ ] Show and change attributes in `src/attrib.c`, never through a link. Done when: a link's target is unchanged. Cheaper substitute: following links.
- [ ] Copy folders in `src/xcopy.c` so that a file is complete or absent. Done when: killing it at any moment leaves no half file. Cheaper substitute: writing in place.
- [ ] Commit: `command: tree, attrib and xcopy`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## 5. robocopy

Mirroring and copying with retry, filters, logging and a dry run. Deleting is only ever what the user asked for.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/robocopy.c`. Done when: each tool's documented cases pass.
2. Compare with Windows' own tool. Done when: the comparison test passes.

- [ ] Build the list of work first and show it with the list switch. Done when: the list equals what a real run does. Cheaper substitute: acting while walking.
- [ ] Mirror without following links out of the target. Done when: a junction in the target is removed and its contents stay. Cheaper substitute: following it.
- [ ] Give robocopy's own exit codes and summary. Done when: each code has a case. Cheaper substitute: 0 and 1.
- [ ] Commit: `command: robocopy`

**Test checkpoint:** `python3 ../ImpossibleSDK/tools/prove.py .` exits 0 after the commit (the file tests run on Windows only) and the new tests fail when their production change is reverted.

## Verification

- Every tool's cases are compared with Windows' own tool on this machine.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
