AC_INIT([FMPTools], [0.0.1], [emmiller@gmail.com], [fmptools], [https://github.com/evanmiller/FMPTools])

AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_SILENT_RULES([yes])

LT_INIT([disable-static])

AC_PROG_CC

AM_ICONV

AC_CHECK_LIB([xlsxwriter], [workbook_new], [true], [false])
AM_CONDITIONAL([HAVE_XLSXWRITER], test "$ac_cv_lib_xlsxwriter_workbook_new" = yes)

AC_CHECK_LIB([yajl], [yajl_gen_alloc], [true], [false])
AM_CONDITIONAL([HAVE_YAJL], test "$ac_cv_lib_yajl_yajl_gen_alloc" = yes)

AC_OUTPUT([Makefile])

AC_MSG_RESULT([
Configuration:

C compiler: $CC
CFLAGS: $CFLAGS
LD: $LD])
