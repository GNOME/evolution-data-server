NULL =

if WITH_PRIVATE_DOCS
PRIVATE_DIR = private
endif

SUBDIRS = \
	eds	\
	camel \
	$(PRIVATE_DIR) \
	$(NULL)

MKDB_OPTIONS=--output-format=xml --name-space=e

-include $(top_srcdir)/git.mk
