#ifndef CAMEL_IMAP_PRIVATE_H
#define CAMEL_IMAP_PRIVATE_H

/* need a way to configure and save this data, if this header is to
 * be installed.  For now, dont install it */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

G_BEGIN_DECLS

struct _CamelImapFolderPrivate {
	GStaticMutex search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */
	GHashTable *ignore_recent;	/* hash table of UIDs to ignore as recent when updating folder */
	gboolean check_folder;		/* persistent property */
	gboolean apply_filters;		/* persistent property */
};

#define CAMEL_IMAP_FOLDER_LOCK(f, l) \
	(g_static_mutex_lock (&((CamelImapFolder *) f)->priv->l))
#define CAMEL_IMAP_FOLDER_UNLOCK(f, l) \
	(g_static_mutex_unlock (&((CamelImapFolder *) f)->priv->l))
#define CAMEL_IMAP_FOLDER_REC_LOCK(f, l) \
	(g_static_rec_mutex_lock (&((CamelImapFolder *) f)->priv->l))
#define CAMEL_IMAP_FOLDER_REC_UNLOCK(f, l) \
	(g_static_rec_mutex_unlock (&((CamelImapFolder *) f)->priv->l))

struct _CamelImapWrapperPrivate {
	GMutex *lock;
};

#define CAMEL_IMAP_WRAPPER_LOCK(f, l) \
	(g_mutex_lock (((CamelImapWrapper *) f)->priv->l))
#define CAMEL_IMAP_WRAPPER_UNLOCK(f, l) \
	(g_mutex_unlock (((CamelImapWrapper *) f)->priv->l))

G_END_DECLS

#endif /* CAMEL_IMAP_PRIVATE_H */
