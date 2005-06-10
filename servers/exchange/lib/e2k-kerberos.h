/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __E2K_KERBEROS_H__
#define __E2K_KERBEROS_H__

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

typedef enum {
	E2K_KERBEROS_OK,
	E2K_KERBEROS_USER_UNKNOWN,
	E2K_KERBEROS_PASSWORD_INCORRECT,
	E2K_KERBEROS_PASSWORD_EXPIRED,
	E2K_KERBEROS_PASSWORD_TOO_WEAK,

	E2K_KERBEROS_KDC_UNREACHABLE,
	E2K_KERBEROS_TIME_SKEW,

	E2K_KERBEROS_FAILED,
} E2kKerberosResult;

E2kKerberosResult e2k_kerberos_check_password  (const char *user,
						const char *domain,
						const char *password);

E2kKerberosResult e2k_kerberos_change_password (const char *user,
						const char *domain,
						const char *old_password,
						const char *new_password);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E2K_FREEBUSY_H__ */
