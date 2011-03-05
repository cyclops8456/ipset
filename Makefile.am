## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

include $(top_srcdir)/Make_global.am

if ! WITH_MAXSETS
IP_SET_MAX=256
else
IP_SET_MAX=$(MAXSETS)
endif
if ENABLE_VERBOSE
V=1
else
V=0
endif

SUBDIRS		= lib src

