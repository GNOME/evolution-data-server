#ifndef CAMEL_IMAP_PRIVATE_H
#define CAMEL_IMAP_PRIVATE_H 1

/* need a way to configure and save this data, if this header is to
   be installed.  For now, dont install it */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

G_BEGIN_DECLS

struct _CamelImapFolderPrivate {
#ifdef ENABLE_THREADS
	GStaticMutex search_lock;	/* for locking the search object */
	GStaticRecMutex cache_lock;	/* for locking the cache object */
#endif
};

#ifdef ENABLE_THREADS
#define CAMEL_IMAP_FOLDER_LOCK(f, l) \
	(g_static_mutex_lock(&((CamelImapFolder *)f)->priv->l))
#define CAMEL_IMAP_FOLDER_UNLOCK(f, l) \
	(g_static_mutex_unlock(&((CamelImapFolder *)f)->priv->l))
#define CAMEL_IMAP_FOLDER_REC_LOCK(f, l) \
	(g_static_rec_mutex_lock(&((CamelImapFolder *)f)->priv->l))
#define CAMEL_IMAP_FOLDER_REC_UNLOCK(f, l) \
	(g_static_rec_mutex_unlock(&((CamelImapFolder *)f)->priv->l))
#else
#define CAMEL_IMAP_FOLDER_LOCK(obj)
#define CAMEL_IMAP_FOLDER_UNLOCK(obj)
#define CAMEL_IMAP_FOLDER_REC_LOCK(obj)
#define CAMEL_IMAP_FOLDER_REC_UNLOCK(obj)
#endif

struct _CamelImapWrapperPrivate {
#ifdef ENABLE_THREADS
	GMutex *lock;
#endif
};

#ifdef ENABLE_THREADS
#define CAMEL_IMAP_WRAPPER_LOCK(f, l) (g_mutex_lock(((CamelImapWrapper *)f)->priv->l))
#define CAMEL_IMAP_WRAPPER_UNLOCK(f, l) (g_mutex_unlock(((CamelImapWrapper *)f)->priv->l))
#else
#define CAMEL_IMAP_WRAPPER_LOCK(f, l)
#define CAMEL_IMAP_WRAPPER_UNLOCK(f, l)
#endif

G_END_DECLS

#endif /* CAMEL_IMAP_PRIVATE_H */
