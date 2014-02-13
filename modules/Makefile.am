NULL =

if HAVE_GOA
GNOME_ONLINE_ACCOUNTS_DIR = gnome-online-accounts
endif

if HAVE_GTK
TRUST_PROMPT_DIR = trust-prompt
endif

if HAVE_UOA
UBUNTU_ONLINE_ACCOUNTS_DIR = ubuntu-online-accounts
endif

SUBDIRS = \
	cache-reaper \
	google-backend \
	outlook-backend \
	owncloud-backend \
	secret-monitor \
	yahoo-backend \
	$(TRUST_PROMPT_DIR) \
	$(GNOME_ONLINE_ACCOUNTS_DIR) \
	$(UBUNTU_ONLINE_ACCOUNTS_DIR) \
	$(NULL)

-include $(top_srcdir)/git.mk
