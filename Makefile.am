AUTOMAKE_OPTIONS = foreign dist-zip

lib_LTLIBRARIES = libfmptools.la
noinst_PROGRAMS = fmpdump
bin_PROGRAMS =
include_HEADERS = src/fmp.h
noinst_HEADERS = src/fmp_internal.h

if HAVE_XLSXWRITER
bin_PROGRAMS += fmp2excel

fmp2excel_SOURCES = src/bin/fmp2excel.c
fmp2excel_LDADD = libfmptools.la -lxlsxwriter
endif

if HAVE_YAJL
bin_PROGRAMS += fmp2json

fmp2json_SOURCES = src/bin/fmp2json.c
fmp2json_LDADD = libfmptools.la -lyajl
endif

fmpdump_SOURCES = src/bin/fmpdump.c
fmpdump_LDADD = libfmptools.la

libfmptools_la_SOURCES = \
	src/block.c \
	src/dump_file.c \
	src/fmp.c \
	src/list_tables.c \
	src/read_values.c

libfmptools_la_LIBADD = @LIBICONV@
