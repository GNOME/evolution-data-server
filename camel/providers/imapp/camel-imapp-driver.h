
#ifndef _CAMEL_IMAPP_DRIVER_H
#define _CAMEL_IMAPP_DRIVER_H

#include <camel/camel-list-utils.h>
#include <camel/camel-object.h>

#include "camel-imapp-stream.h"

#define CAMEL_IMAPP_DRIVER_TYPE     (camel_imapp_driver_get_type ())
#define CAMEL_IMAPP_DRIVER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAPP_DRIVER_TYPE, CamelIMAPPDriver))
#define CAMEL_IMAPP_DRIVER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAPP_DRIVER_TYPE, CamelIMAPPDriverClass))
#define CAMEL_IS_IMAP_DRIVER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAPP_DRIVER_TYPE))

G_BEGIN_DECLS

typedef struct _CamelIMAPPDriver CamelIMAPPDriver;
typedef struct _CamelIMAPPDriverClass CamelIMAPPDriverClass;

typedef struct _CamelIMAPPFetch CamelIMAPPFetch;

typedef gint (*CamelIMAPPDriverFunc)(struct _CamelIMAPPDriver *driver, gpointer data);
typedef struct _CamelSasl * (*CamelIMAPPSASLFunc)(struct _CamelIMAPPDriver *driver, gpointer data);
typedef void (*CamelIMAPPLoginFunc)(struct _CamelIMAPPDriver *driver, gchar **login, gchar **pass, gpointer data);

typedef void (*CamelIMAPPFetchFunc)(struct _CamelIMAPPDriver *driver, CamelIMAPPFetch *);

struct _CamelIMAPPFetch {
	struct _CamelIMAPPFetch *next;
	struct _CamelIMAPPFetch *prev;

	CamelStream *body;	/* the content fetched */

	struct _CamelIMAPPFolder *folder;
	gchar *uid;
	gchar *section;

	CamelIMAPPFetchFunc done;
	gpointer data;
};

struct _CamelMimeMessage;

struct _CamelIMAPPDriver {
	CamelObject parent_object;

	struct _CamelIMAPPEngine *engine;

	struct _CamelIMAPPFolder *folder;

	/* current folder stuff */
	GPtrArray *summary;
	guint32 uidvalidity;
	guint32 exists;
	guint32 recent;
	guint32 unseen;
	guint32 permanentflags;

	/* list stuff */
	GPtrArray *list_result;
	GSList *list_commands;
	guint32 list_flags;

	/* sem_t list_sem; for controlled access to list variables */

	/* this is so the node is always in a list - easier exception management */
	CamelDList body_fetch;
	CamelDList body_fetch_done;

	/* factory to get an appropriate sasl mech */
	CamelIMAPPSASLFunc get_sasl;
	gpointer get_sasl_data;

	/* callbacks, get login username/pass */
	CamelIMAPPLoginFunc get_login;
	gpointer get_login_data;
};

struct _CamelIMAPPDriverClass {
	CamelObjectClass parent_class;
};

CamelType	camel_imapp_driver_get_type (void);

CamelIMAPPDriver *	camel_imapp_driver_new(CamelIMAPPStream *stream);

void			camel_imapp_driver_set_sasl_factory(CamelIMAPPDriver *id, CamelIMAPPSASLFunc get_sasl, gpointer sasl_data);
void			camel_imapp_driver_set_login_query(CamelIMAPPDriver *id, CamelIMAPPLoginFunc get_login, gpointer login_data);

void			camel_imapp_driver_login(CamelIMAPPDriver *id);

void			camel_imapp_driver_select(CamelIMAPPDriver *id, struct _CamelIMAPPFolder *folder);
void			camel_imapp_driver_update(CamelIMAPPDriver *id, struct _CamelIMAPPFolder *folder);
void			camel_imapp_driver_sync(CamelIMAPPDriver *id, gboolean expunge, struct _CamelIMAPPFolder *folder);

struct _CamelStream *	camel_imapp_driver_fetch(CamelIMAPPDriver *id, struct _CamelIMAPPFolder *folder, const gchar *uid, const gchar *body);

GPtrArray *		camel_imapp_driver_list(CamelIMAPPDriver *id, const gchar *name, guint32 flags);

struct _CamelStream *camel_imapp_driver_get(CamelIMAPPDriver *id, struct _CamelIMAPPFolder *folder, const gchar *uid);
void camel_imapp_driver_append(CamelIMAPPDriver *id, struct _CamelIMAPPFolder *folder, struct _CamelDataWrapper *);

G_END_DECLS

#endif
