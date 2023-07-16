# mincbuild

## Introduction
mincbuild, or (Min)imal (C) (Build)system, is a program which builds a C(++)
project with a simple directory structure. All configuration for the build
process is specified in a JSON file, so there is no requirement of learning any
annoying DSL like with Make. Please note that mincbuild enforces a project
structure which is *only good for small-to-medium-sized programs*. Hell, it's
minimal software designed to help with building minimal software. If your
project is going to have tons of interlocking parts f\*cking about with each
other and reaching into each others directories, look somewhere else... And
perhaps redesign your project...

Honestly, the only person this is intended for is myself, but if you know me
personally and would like to use it; ask me and I will explain it to you. Maybe
I'll write a guide some day.

## Dependencies
mincbuild is designed for Linux. Do not expect it to work (or even compile) on
your Windows machine.

Software / system dependencies are:
* `json-c`, `pthread`, `libtmcul` C libraries
* `mkdir`, `rmdir`, `grep`, `nproc` programs available on system
* A shell environment for program execution

## Management
* To bootstrap the program, run `./bootstrap.sh`
* To rebuild using the bootstrapped program, run `./mincbuild`
* To install the program, run `./install.sh`
* To remove program files from system, run `./uninstall.sh`

## Usage
After installation,
1. Navigate to a project directory containing the mincbuild JSON file
2. Run `mincbuild` if the mincbuild JSON file is called `mincbuild.json`, and
`mincbuild file.json` if the file is called `file.json`

## Contributing
I am not accepting pull requests unless they refactor code to make it smaller
and more readable. I am definitely not accepting pull requests which implement
features unless I *really* like them. Feel free to fork this project and make
your own version.

## Vulnerability note
mincbuild is designed to be extremely simple and only do the bare minimum of
building a C project. For the sake of simplicity, it makes use of the `system()`
and `popen()` functions; as to avoid reimplementing `grep`, `mkdir -p`, etc.
following the minimalist software philosophy. The commands run are partially
determined by user input through the build config, so some sanitization is
necessary. The following things are done:

* Any of the following characters are prefixed with '\\': `; ' " < >`
* Any lone backslashes are replaced by "\\\\"

Nothing else is done for sanitization, but it should be fairly simple to add
more in case you really need a highly secure system or use a really weird shell.
To do so, modify the function `sanitize_cmd()` in `src/util.c`.
