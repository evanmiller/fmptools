[![Build Status](https://github.com/evanmiller/fmptools/workflows/build/badge.svg)](https://github.com/evanmiller/fmptools/actions)

FMP Tools
--

Some tools for reading FileMaker Pro files (fp3, fp5, fp7, and fmp12). See the
included [HACKING](./HACKING) file for technical information on the FileMaker
format.

Building from the git source first requires [autoconf](https://www.gnu.org/software/autoconf/):

```
autoreconf -i -f
```

Building from a release requires the usual:

```
./configure
make
make install
```

The tools installed to `$PREFIX/bin` include:

* `fmp2excel` - Convert a FileMaker Pro database to Excel (requires [libxlsxwriter](http://libxlsxwriter.github.io))
* `fmp2json` - Convert a FileMaker Pro database to JSON (requires [yajl](https://lloyd.github.io/yajl/))
* `fmp2sqlite` - Convert a FileMaker Pro database to SQLite (requires [sqlite](https://www.sqlite.org/index.html))

The source tree also builds a local helper, `fmpobjextract`, for extracting a
payload from a FileMaker container reference. It is not installed by
`make install`.

```
./fmpobjextract <file> <container-value> <output-file> [--debug-prefix <prefix>]
```

`container-value` is the raw value stored in the container field, such as
`842D` or `807E`. The tool decodes that reference, finds the best available
representation in the object store, and writes the reconstructed payload to
`output-file`. `--debug-prefix` additionally writes intermediate dumps and a
manifest.

There is also a C library installed that is used by the above tools, but the
API is subject to change.

You might also enjoy [fp5dump](https://github.com/qwesda/fp5dump), although
that project does not read the newer fp7 and fmp12 formats.
