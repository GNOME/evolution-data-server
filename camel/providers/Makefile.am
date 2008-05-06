## Process this file with automake to produce Makefile.in

if ENABLE_NNTP
NNTP_DIR=nntp
endif

if ENABLE_IMAPP
IMAPP_DIR=imapp
endif

if ENABLE_IMAP4
IMAP4_DIR=imap4
endif

if OS_WIN32
else
SENDMAIL_DIR=sendmail
endif

SUBDIRS = pop3 $(SENDMAIL_DIR) smtp imap $(IMAP4_DIR) $(NNTP_DIR) local $(IMAPP_DIR) groupwise hula


