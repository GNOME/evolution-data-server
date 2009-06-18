/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __E2K_KERBEROS_H__
#define __E2K_KERBEROS_H__

G_BEGIN_DECLS

typedef enum {
	E2K_KERBEROS_OK,
	E2K_KERBEROS_USER_UNKNOWN,
	E2K_KERBEROS_PASSWORD_INCORRECT,
	E2K_KERBEROS_PASSWORD_EXPIRED,
	E2K_KERBEROS_PASSWORD_TOO_WEAK,

	E2K_KERBEROS_KDC_UNREACHABLE,
	E2K_KERBEROS_TIME_SKEW,

	E2K_KERBEROS_FAILED
} E2kKerberosResult;

E2kKerberosResult e2k_kerberos_check_password  (const gchar *user,
						const gchar *domain,
						const gchar *password);

E2kKerberosResult e2k_kerberos_change_password (const gchar *user,
						const gchar *domain,
						const gchar *old_password,
						const gchar *new_password);

G_END_DECLS

#endif /* __E2K_FREEBUSY_H__ */
