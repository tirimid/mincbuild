# mincbuild

## Introduction
mincbuild, or (M)inimal (C) (Build)system, is a program which builds a C(++)
project with a simple directory structure. All configuration for the build
process is specified in a JSON file, so there is no requirement of learning any
annoying DSL like with Make.

## Vulnerability note
mincbuild is designed to be extremely simple and only do the bare minimum of
building a C project. For the sake of simplicity, it makes use of the `system()`
and `popen()` functions; as to avoid reimplementing `grep`, `mkdir -p`, etc.
following the minimalist software philosophy. The commands run are partially
determined by user input through the build config, so some sanitization is
necessary. The following things are done:

* Any whitespace characters are prefixed with "\"
* Any lone semicolons are replaced by "\;"
* Any lone backslashes are replaced by "\\"
* Any quote characters are prefixed with "\"

Nothing else is done for sanitization, but it should be fairly simple to add
more in case you really need a highly secure system or use a really weird shell.
To do so, modify the function `sanitize_cmd()` in `src/util.c`.

## Dependencies
mincbuild is designed for Linux. Do not expect it to work (or even compile) on
your Windows machine.

Software / system dependencies are:
* `json-c`, `pthread` C libraries
* `mkdir`, `rmdir`, `grep` programs available on system
* A shell environment for program execution

## Building
1. Run `./bootstrap.sh` for bootstrapping mincbuild
2. Run `./mincbuild`, rebuilding the now-selfhosted project
