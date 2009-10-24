
#ifndef _E_MSGPORT_H
#define _E_MSGPORT_H

#include <time.h>
#include <glib.h>

#ifndef EDS_DISABLE_DEPRECATED
/* double-linked list yeah another one, deal */
typedef struct _EDListNode {
	struct _EDListNode *next;
	struct _EDListNode *prev;
} EDListNode;

typedef struct _EDList {
	struct _EDListNode *head;
	struct _EDListNode *tail;
	struct _EDListNode *tailpred;
} EDList;

#define E_DLIST_INITIALISER(l) { (EDListNode *)&l.tail, NULL, (EDListNode *)&l.head }

void e_dlist_init(EDList *v);
EDListNode *e_dlist_addhead(EDList *l, EDListNode *n);
EDListNode *e_dlist_addtail(EDList *l, EDListNode *n);
EDListNode *e_dlist_remove(EDListNode *n);
EDListNode *e_dlist_remhead(EDList *l);
EDListNode *e_dlist_remtail(EDList *l);
gint e_dlist_empty(EDList *l);
gint e_dlist_length(EDList *l);
#endif

/* a time-based cache */
typedef struct _EMCache EMCache;
typedef struct _EMCacheNode EMCacheNode;

/* subclass this for your data nodes, EMCache is opaque */
struct _EMCacheNode {
	struct _EMCacheNode *next, *prev;
	gchar *key;
	gint ref_count;
	time_t stamp;
};

EMCache *em_cache_new(time_t timeout, gsize nodesize, GFreeFunc nodefree);
void em_cache_destroy(EMCache *emc);
EMCacheNode *em_cache_lookup(EMCache *emc, const gchar *key);
EMCacheNode *em_cache_node_new(EMCache *emc, const gchar *key);
void em_cache_node_unref(EMCache *emc, EMCacheNode *n);
void em_cache_add(EMCache *emc, EMCacheNode *n);
void em_cache_clear(EMCache *emc);

#ifndef EDS_DISABLE_DEPRECATED
/* message ports - a simple inter-thread 'ipc' primitive */
/* opaque handle */
typedef struct _EMsgPort EMsgPort;

/* header for any message */
typedef struct _EMsg {
	EDListNode ln;
	EMsgPort *reply_port;
	gint flags;
} EMsg;

EMsgPort *e_msgport_new(void);
void e_msgport_destroy(EMsgPort *mp);
/* get a fd that can be used to wait on the port asynchronously */
gint e_msgport_fd(EMsgPort *mp);
void e_msgport_put(EMsgPort *mp, EMsg *msg);
EMsg *e_msgport_wait(EMsgPort *mp);
EMsg *e_msgport_get(EMsgPort *mp);
void e_msgport_reply(EMsg *msg);
#ifdef HAVE_NSS
struct PRFileDesc *e_msgport_prfd(EMsgPort *mp);
#endif
#endif /* EDS_DISABLE_DEPRECATED */

#ifndef EDS_DISABLE_DEPRECATED
/* e threads, a server thread with a message based request-response, and flexible queuing */
typedef struct _EThread EThread;

typedef enum {
	E_THREAD_QUEUE = 0,	/* run one by one, until done, if the queue_limit is reached, discard new request */
	E_THREAD_DROP,		/* run one by one, until done, if the queue_limit is reached, discard oldest requests */
	E_THREAD_NEW		/* always run in a new thread, if the queue limit is reached, new requests are
				   stored in the queue until a thread becomes available for it, creating a thread pool */
} e_thread_t;

typedef void (*EThreadFunc)(EThread *, EMsg *, gpointer data);

EThread *e_thread_new(e_thread_t type);
void e_thread_destroy(EThread *e);
void e_thread_set_queue_limit(EThread *e, gint limit);
void e_thread_set_msg_lost(EThread *e, EThreadFunc destroy, gpointer data);
void e_thread_set_msg_destroy(EThread *e, EThreadFunc destroy, gpointer data);
void e_thread_set_reply_port(EThread *e, EMsgPort *reply_port);
void e_thread_set_msg_received(EThread *e, EThreadFunc received, gpointer data);
void e_thread_put(EThread *e, EMsg *msg);
gint e_thread_busy(EThread *e);
#endif /* EDS_DISABLE_DEPRECATED */

#ifndef EDS_DISABLE_DEPRECATED
/* sigh, another mutex interface, this one allows different mutex types, portably */
typedef struct _EMutex EMutex;

typedef enum _e_mutex_t {
	E_MUTEX_SIMPLE,		/* == pthread_mutex */
	E_MUTEX_REC		/* recursive mutex */
} e_mutex_t;

EMutex *e_mutex_new(e_mutex_t type);
gint e_mutex_destroy(EMutex *m);
gint e_mutex_lock(EMutex *m);
gint e_mutex_unlock(EMutex *m);
void e_mutex_assert_locked(EMutex *m);
/* this uses pthread cond's */
gint e_mutex_cond_wait(gpointer cond, EMutex *m);
#endif /* EDS_DISABLE_DEPRECATED */

#endif
