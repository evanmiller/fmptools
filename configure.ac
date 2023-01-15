AC_INIT([FMPTools],[0.2.1],[https://github.com/evanmiller/fmptools/issues],[fmptools],[https://github.com/evanmiller/fmptools])

AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_SILENT_RULES([yes])

LT_INIT([])

AC_PROG_CC
AC_PROG_CXX

AM_ICONV

AC_CHECK_FUNCS(strptime fmemopen)

AC_CHECK_LIB([xlsxwriter], [workbook_new], [true], [false])
AM_CONDITIONAL([HAVE_XLSXWRITER], test "$ac_cv_lib_xlsxwriter_workbook_new" = yes)

AC_CHECK_LIB([yajl], [yajl_gen_alloc], [true], [false])
AM_CONDITIONAL([HAVE_YAJL], test "$ac_cv_lib_yajl_yajl_gen_alloc" = yes)

AC_CHECK_LIB([sqlite3], [sqlite3_open_v2], [true], [false])
AM_CONDITIONAL([HAVE_SQLITE], test "$ac_cv_lib_sqlite3_sqlite3_open_v2" = yes)

dnl Fuzz testing
AC_ARG_ENABLE([fuzz-testing], AS_HELP_STRING([--enable-fuzz-testing], ["Enable fuzz testing (requires Clang 6 or later)"]), [
   AC_MSG_CHECKING([whether $CC accepts -fsanitize=fuzzer])
   tmp_saved_flags=$[]_AC_LANG_PREFIX[]FLAGS
   _AC_LANG_PREFIX[]FLAGS="$[]_AC_LANG_PREFIX[]FLAGS -fsanitize=fuzzer"
   AC_LINK_IFELSE([AC_LANG_PROGRAM()],
       [
        AC_MSG_RESULT(yes)
        OPTDIRS="$OPTDIRS fuzz"
        fuzzer=yes],
        AC_MSG_RESULT(no)
       AC_MSG_FAILURE([-fsanitize=fuzzer not supported (Required with --enable-fuzz-testing)]))
   _AC_LANG_PREFIX[]FLAGS=$tmp_saved_flags
], [fuzzer=no])
AM_CONDITIONAL([FUZZER_ENABLED], test "x$fuzzer" = "xyes")

AC_ARG_VAR([LIB_FUZZING_ENGINE], [Location of prebuilt fuzzing engine library])
AC_SUBST([LIB_FUZZING_ENGINE])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_MSG_RESULT([
Configuration:

C compiler: $CC
CFLAGS: $CFLAGS
LD: $LD

Tools to build:

fmp2sqlite: $ac_cv_lib_sqlite3_sqlite3_open_v2
fmp2excel: $ac_cv_lib_xlsxwriter_workbook_new
fmp2json: $ac_cv_lib_yajl_yajl_gen_alloc])
