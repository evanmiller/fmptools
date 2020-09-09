AUTOMAKE_OPTIONS = foreign dist-zip

lib_LTLIBRARIES = libfmptools.la
noinst_PROGRAMS = fmpdump
bin_PROGRAMS =
include_HEADERS = src/fmp.h
noinst_HEADERS = src/fmp_internal.h src/bin/usage.h

EXTRA_PROGRAMS =
AM_CFLAGS =

if HAVE_XLSXWRITER
bin_PROGRAMS += fmp2excel

fmp2excel_SOURCES = src/bin/fmp2excel.c src/bin/usage.c
fmp2excel_LDADD = libfmptools.la -lxlsxwriter
endif

if HAVE_YAJL
bin_PROGRAMS += fmp2json

fmp2json_SOURCES = src/bin/fmp2json.c src/bin/usage.c
fmp2json_LDADD = libfmptools.la -lyajl
endif

if HAVE_SQLITE
bin_PROGRAMS += fmp2sqlite

fmp2sqlite_SOURCES = src/bin/fmp2sqlite.c src/bin/usage.c
fmp2sqlite_LDADD = libfmptools.la -lsqlite3
endif

fmpdump_SOURCES = src/bin/fmpdump.c
fmpdump_LDADD = libfmptools.la

libfmptools_la_SOURCES = \
	src/block.c \
	src/dump_file.c \
	src/fmp.c \
	src/list_columns.c \
	src/list_tables.c \
	src/read_values.c

libfmptools_la_LIBADD = @LIBICONV@
libfmptools_la_CFLAGS = -Wall -Werror -pedantic-errors
libfmptools_la_LDFLAGS = -export-symbols-regex '^fmp_'

if FUZZER_ENABLED
EXTRA_PROGRAMS += fuzz_fmp
# Force C++ linking for fuzz target
nodist_EXTRA_fuzz_fmp_SOURCES = dummy.cxx
fuzz_fmp_SOURCES = src/fuzz/fuzz_fmp.c
fuzz_fmp_LDFLAGS = -static
fuzz_fmp_LDADD = libfmptools.la @LIB_FUZZING_ENGINE@

AM_CFLAGS += -fsanitize=fuzzer-no-link -fsanitize=address -g
libfmptools_la_LDFLAGS += -fsanitize=fuzzer -fsanitize=address
endif
