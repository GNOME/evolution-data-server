if ENABLE_NNTP
NNTP_DIR=nntp
endif

if OS_WIN32
else
SENDMAIL_DIR=sendmail
endif

SUBDIRS = pop3 $(SENDMAIL_DIR) smtp imap imapx $(NNTP_DIR) local

-include $(top_srcdir)/git.mk
