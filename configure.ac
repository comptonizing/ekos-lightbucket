AC_INIT([ekos-lightbucket], [0.1], [philipp@whyisitso.de])

AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_CONFIG_MACRO_DIRS([m4])

AC_PROG_CC
AC_PROG_CXX

AM_PROG_AR

LT_PREREQ([2.4.6])
LT_INIT

CXXFLAGS="$CXXFLAGS -g -pedantic -W -Wall"
LDFLAGS="$LDFLAGS -g -W -Wall"

# Enable debugging
AC_ARG_ENABLE([debugging], AS_HELP_STRING([--enable-debugging], [Enable debugging statements]), [CXXFLAGS="-D ISDEBUG $CXXFLAGS"], [])

# LIBRARIES
AC_CHECK_HEADERS([math.h string.h stdlib.h glib.h fitsio.h])

AC_SEARCH_LIBS(sin, m, [], [AC_MSG_ERROR([No math library found!])], [])
AC_SEARCH_LIBS(g_log, glib-2.0, [], [AC_MSG_ERROR([libglib-2.0 not found!])], [])
AC_SEARCH_LIBS(gtk_application_new, gtk-3, [], [AC_MSG_ERROR([gtk-3 library not found!])])
AC_SEARCH_LIBS([_ZN3Gtk11Application3runEv], [gtkmm-3.0], [], [AC_MSG_ERROR([gtkmm-3.0 not found!])], [])
AC_SEARCH_LIBS([_ZN4Glib7ustringC1EPKc], [glibmm-2.4], [], [AC_MSG_ERROR([glibmm-2.4 not found!])], [])
AC_SEARCH_LIBS([ffopen], [cfitsio], [], [AC_MSG_ERROR([libcfitsio not found!])])

PKG_CHECK_MODULES([GTK], [gtk+-3.0])
CXXFLAGS="${CXXFLAGS} ${GTK_CFLAGS}"
LIBS="${LIBS} ${GTK_LIBS}"
PKG_CHECK_MODULES([GTKMM], [gtkmm-3.0 >= 3.24.2])
CXXFLAGS="${CXXFLAGS} ${GTKMM_CFLAGS}"
LIBS="${LIBS} ${GTKMM_LIBS}"

AC_SUBST([CXXFLAGS], [$CXXFLAGS])
AC_SUBST([LDFLAGS], [$LDFLAGS])

AC_CONFIG_FILES([Makefile
		 src/Makefile])

AC_OUTPUT