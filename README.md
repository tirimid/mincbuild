# mincbuild

## Introduction
mincbuild, or (Min)imal (C) (Build)system, is a highly minimal program which
builds a C(++) project with a simple directory structure.

Honestly, the only person this is intended for is myself, but if you know me
personally and would like to use it; ask me and I will explain it to you. Maybe
I'll write a guide some day.

## Dependencies
mincbuild is designed for Linux. Do not expect it to work (or even compile) on
your Windows machine.

Software / system dependencies are:
* A shell environment for program execution

## Management
* To bootstrap the program, run `./bootstrap.sh`
* To rebuild using the bootstrapped program, run `./mincbuild`
* To install the program, run `./install.sh`
* To remove program files from system, run `./uninstall.sh`

## Usage
After installation,
1. Navigate to a project directory containing the mincbuild config file
2. Run `mincbuild` if the mincbuild config file is called `mincbuild.conf`, and
`mincbuild file.conf` if the file is called `file.conf`

## Contributing
I am not accepting pull requests unless they refactor code to make it smaller
and more readable. I am definitely not accepting pull requests which implement
features unless I *really* like them. Feel free to fork this project and make
your own version.
