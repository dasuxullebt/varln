# varln
Variant Symlinks via FUSE

## Compiling

This is still at an early stage of development, I did not yet create a
proper Makefile. It should be possible to compile under Linux via

    cc -c envs.c
    cc -o varln varln.c envs.o `pkg-config --cflags fuse` `pkg-config --libs fuse`

assuming you have a correct installation of FUSE.

## Testing

Currently, only a very simple configuration format is defined: Regular
files inside a directory are directly mapped to symbolic links in the
mounted directory. These regular files must contain two lines. The
first line denotes the *fallback* destination, the second line
contains the name of an environment variable. Example:

    /tmp
    TEST_FS

Save this into a file `test` inside a directory, for example
`/tmp/testconf/`. Then create a directory `/tmp/testmnt/`. To mount, run

    ./varln /tmp/testconf /tmp/testmnt

Now, you will notice

    $ ls -l /tmp/testmnt
    total 4
    l--------- 1 senjak senjak 14 Jan 11 00:00 test -> /tmp

However

    $ TEST_FS=/var/run ls -l /tmp/testmnt
    total 4
    l--------- 1 senjak senjak 14 Jan 11 00:00 test -> /var/run

## License

Copyright (C) 2016 Christoph-Simon Senjak

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.

## See also

[varsymfs](https://github.com/onslauth/varsymfs) seems to be a project
that aims to create variant symlinks in the kernel.