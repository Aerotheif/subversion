dnl  SVN_LIB_APR(version)
dnl
dnl  Check configure options and assign variables related to
dnl  the Apache Portable Runtime (APR) library.
dnl
dnl  If there is a apr/ subdir we assme we want to use it. In that
dnl  case an option telling us to use a locally installed apr
dnl  triggers an error.
dnl
dnl  TODO : check apr version, link a test program


AC_DEFUN(SVN_LIB_APR,
[

  AC_MSG_NOTICE([Apache Portable Runtime (APR) library configuration])

  AC_ARG_WITH(apr-libs,
              [AC_HELP_STRING([--with-apr-libs=PREFIX],
	      [Use Apache Portable Runtime (APR) library at PREFIX])],
  [
    if test -d $abs_srcdir/apr ; then
      AC_MSG_ERROR([--with-apr-libs option but apr/ subdir exists.
Please either remove that subdir or don't use the --with-apr-libs option.])
    fi

    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-apr-libs requires an argument.])
    else
      APRVARS="$withval/APRVARS"
      APR_LIBS="$withval"
    fi
  ])

  AC_ARG_WITH(apr-includes,
              [AC_HELP_STRING([--with-apr-includes=PREFIX],
	      [Use Apache Portable Runtime (APR) includes at PREFIX])],
  [
    if test -d $abs_srcdir/apr ; then
      AC_MSG_ERROR([--with-apr-includes option but apr/ subdir exists.
Please either remove that subdir or don't use the --with-apr-includes option.])
    fi

    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-apr-includes requires an argument.])
    else
      APR_INCLUDES="$withval"
    fi
  ])

  AC_ARG_WITH(apr,
              [AC_HELP_STRING([--with-apr=PREFIX],
	      [Use Apache Portable Runtime (APR) at PREFIX])],
  [
    if test -d $abs_srcdir/apr ; then
      AC_MSG_ERROR([--with-apr option but apr/ subdir exists.
Please either remove that subdir or don't use the --with-apr option.])
    fi

    if test "$withval" != "yes" ; then
      APR_INCLUDES="$withval/include"
      APRVARS="$withval/lib/APRVARS"
      APR_LIBS="$withval/lib"
    fi
  ])

  if test -d $abs_srcdir/apr ; then
    echo "Using apr found in source directory"
    APR_INCLUDES='$(abs_builddir)/apr/include'
    APR_LIBS='$(abs_builddir)/apr'
    APRVARS=$abs_builddir/apr/APRVARS
    SVN_SUBDIR_CONFIG(apr)
    SVN_SUBDIRS="$SVN_SUBDIRS apr"
  else
    SVN_FIND_APR
  fi


  dnl Get libraries and thread flags from APR ---------------------

  if test -f "$APRVARS"; then
    . "$APRVARS"
    CPPFLAGS="$CPPFLAGS $EXTRA_CPPFLAGS"
    CFLAGS="$CFLAGS $EXTRA_CFLAGS"
    LIBS="$LIBS $EXTRA_LIBS"
  else
    AC_MSG_WARN([APRVARS not found])
    SVN_DOWNLOAD_APR
  fi

  if test -n "$APR_INCLUDES" ; then
    SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES -I$APR_INCLUDES"
    if test "$abs_srcdir" != "$abs_builddir" && test -d $abs_srcdir/apr ; then
      SVN_EXTRA_INCLUDES="$SVN_EXTRA_INCLUDES -I$abs_srcdir/apr/include"
    fi
  fi

  if test -z "$APR_LIBS" ; then
    SVN_APR_LIBS="-lapr $LIBTOOL_LIBS"
  else
    SVN_APR_LIBS="$APR_LIBS/libapr.la $LIBTOOL_LIBS"
  fi
  AC_SUBST(SVN_APR_LIBS)

])

dnl SVN_FIND_APR()
dnl Look in standard places for APRVARS, apr.h, and -lapr.
AC_DEFUN(SVN_FIND_APR,
[
  CPPFLAGS_save=$CPPFLAGS
  if test -n "$APR_INCLUDES" ; then
    CPPFLAGS="$CPPFLAGS -I$APR_INCLUDES"
  fi
  AC_CHECK_HEADER(apr.h, apr_h="yes", apr_h="no")
  if test "$apr_h" = "no" ; then
    echo "Couldn't find apr.h"
    SVN_DOWNLOAD_APR
  fi

  CPPFLAGS=$CPPFLAGS_save
  if test -z "$APRVARS" ; then
    dirs="/etc /usr/lib /usr/local/lib /opt/apr/lib"
    for dir in $dirs; do
      if test -f $dir/APRVARS ; then
        APRVARS=$dir/APRVARS
        break
      fi
    done
  fi
])


dnl SVN_DOWNLOAD_APR()
dnl no apr found, print out a message telling the user what to do
AC_DEFUN(SVN_DOWNLOAD_APR,
[
  echo "No Apache Portable Runtime (APR) library can be found."
  echo "Either install APR on this system and supply appropriate"
  echo "--with-apr-libs and --with-apr-includes options"
  echo ""
  echo "or"
  echo ""
  echo "get it with CVS and put it in a subdirectory of this source:"
  echo ""
  echo "   cvs -d :pserver:anoncvs@cvs.apache.org:/home/cvspublic login"
  echo "      (password 'anoncvs')"
  echo ""
  echo "   cvs -d :pserver:anoncvs@cvs.apache.org:/home/cvspublic co apr"
  echo ""
  echo "Run that right here in the top-level of the Subversion tree."
  echo ""
  AC_MSG_ERROR([no suitable apr found])
])
