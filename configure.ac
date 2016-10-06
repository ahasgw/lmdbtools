#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([chemstgen], [0.0.20161006], [ahasgw@gmail.com])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

# Checks for programs.
AC_LANG_PUSH([C++])
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX(11, ext, mandatory)
AC_OPENMP
AC_SUBST(AM_CXXFLAGS, "$OPENMP_CXXFLAGS")
AC_LANG_POP([C++])
AC_PROG_CC_C99

# Checks for libraries.
AC_CHECK_LIB([gflags], [main], [],
             [AC_MSG_ERROR([libgflags is required])])
AC_CHECK_LIB([lmdb], [main], [],
             [AC_MSG_ERROR([liblmdb is required])])
AC_CHECK_LIB([fmt], [main], [],
             [AC_MSG_ERROR([libfmt is required])])
AC_CHECK_LIB([cryptopp], [main], [],
             [AC_MSG_ERROR([libcryptopp is required])])
AC_CHECK_LIB([helium], [main], [],
             [AC_MSG_ERROR([libheliums is required])])
PKG_CHECK_MODULES([eigen3], [eigen3 >= 3.0.0], [],
                  [AC_MSG_ERROR([eigen3 is required])])

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
