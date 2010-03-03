if ENABLE_NNTP
NNTP_DIR=nntp
endif

if ENABLE_IMAP4
IMAP4_DIR=imap4
endif

if ENABLE_HULA
HULA_DIR=hula
endif

if OS_WIN32
else
IMAPX_DIR=imapx
SENDMAIL_DIR=sendmail
endif

SUBDIRS = pop3 $(SENDMAIL_DIR) smtp imap $(IMAPX_DIR) $(IMAP4_DIR) $(NNTP_DIR) local groupwise $(HULA_DIR)

-include $(top_srcdir)/git.mk
