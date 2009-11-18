EXTRA_DIST =				\
	dummy.la			\
	libedataserverui.def

if OS_WIN32
BOOTSTRAP_LIBS =			\
	libedataserverui-1.2.la
endif

noinst_DATA = $(BOOTSTRAP_LIBS)

libedataserverui_current_minus_age = `expr $(LIBEDATASERVERUI_CURRENT) - $(LIBEDATASERVERUI_AGE)`

libedataserverui-1.2.la: dummy.la libedataserverui.def
	sed -e s!%DLL%!libedataserverui-1.2-$(libedataserverui_current_minus_age)! -e s!%LIB%!libedataserverui-1.2! -e s!%PFX%!$(prefix)! <dummy.la >$@
	mkdir -p .libs
	$(DLLTOOL) --output-lib=.libs/libedataserverui-1.2.dll.a --dllname=libedataserverui-1.2-$(libedataserverui_current_minus_age).dll --input-def=libedataserverui.def

CLEANFILES = $(BOOTSTRAP_LIBS)

-include $(top_srcdir)/git.mk
