# Test ROMs (not included)

This directory intentionally contains no ROM files - `nestest.nes` and other
homebrew test ROMs are third-party tools with their own licensing, so they
aren't bundled in this repository.

To enable the extra CPU accuracy check in `cpu_nestest_test`, download both
files below into this directory (search "nestest.nes" and "nestest.log" -
they're widely mirrored on 6502/NES development wiki and forum pages, e.g.
the wiki.nesdev.org test ROM pages):

- `nestest.nes`
- `nestest.log` (the golden execution trace to compare against)

Without them, `cpu_nestest_test` prints a notice and passes (skipped); the
hand-written `cpu_unit_test` suite runs regardless and needs no external
files.

Later milestones will add Blargg's PPU/APU test ROMs here the same way.
