# mincbuild

## Introduction

mincbuild, or (Min)imal (C) (Build)system, is a highly minimal program which
builds a C(++) project with a simple directory structure.

## Dependencies

mincbuild is designed for Linux. With some trivial hacking, it can work on
Windows machines.

Software / system dependencies are:

* A shell environment for program execution
* pthread (dependency can be removed by compiling with `-DPRUNE_SINGLE_THREAD`
  and `-DCOMPILE_SINGLE_THREAD`)

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
and more readable, or maybe fix minor bugs. If you have feature / large bugfix
proposals, please contact me (through email or otherwise) and suggest them. Feel
free to fork this project and make your own version.
