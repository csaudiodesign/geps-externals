# GePS PD externals library

Library namespace: `geps`

## Contents

- `[fbnet~]`: Pure data external implementing feedback networks as building block for GePS sound modules.

## Installation

```bash
make clean
make
make install
```

The pd-lib-builder Makefile will figure out where to install the library.
To see the install-path, check the build variables:

```bash
make vars | grep installpath
```

Depending on the target OS the install path is not writable by the user. You will need to `sudo make install` (i.e. on Raspbian).

## Usage

When coding in Pd, allways prepend any external name from this library with the _library namespace_: `[geps/fbnet~]`

## Debugging

In order to properly debug a running external, you need to disable the gcc optimisations (`-O0`) in `Makefile.pdlibbuilder`:

```Makefile
alldebug: c.flags += -O0 -g
alldebug: cxx.flags += -O0 -g
```
