/*
 * Copyright 2000, Ximian, Inc.
 */

#ifndef __E_BOOK_BACKEND_LDAP_H__
#define __E_BOOK_BACKEND_LDAP_H__

#include <libedatabook/e-book-backend.h>

#define E_TYPE_BACKEND_LDAP        (e_book_backend_ldap_get_type ())
#define E_BOOK_BACKEND_LDAP(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BACKEND_LDAP, EBookBackendLDAP))
#define E_BOOK_BACKEND_LDAP_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_BOOK_BACKEND_TYPE, EBookBackendLDAPClass))
#define E_IS_BACKEND_LDAP(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BACKEND_LDAP))
#define E_IS_BACKEND_LDAP_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BACKEND_LDAP))
#define E_BOOK_BACKEND_LDAP_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BACKEND_LDAP, EBookBackendLDAPClass))

typedef struct _EBookBackendLDAPPrivate EBookBackendLDAPPrivate;

typedef struct {
	EBookBackend             parent_object;
	EBookBackendLDAPPrivate *priv;
} EBookBackendLDAP;

typedef struct {
	EBookBackendClass parent_class;
} EBookBackendLDAPClass;

EBookBackend *e_book_backend_ldap_new      (void);
GType       e_book_backend_ldap_get_type (void);

#endif /* ! __E_BOOK_BACKEND_LDAP_H__ */

