/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2001-2002
 *	Sleepycat Software.  All rights reserved.
 *
 * $Id$
 */

#ifndef _EX_REPQUOTE_H_
#define	_EX_REPQUOTE_H_

#define	SELF_EID	1

typedef struct {
	gchar *host;		/* Host name. */
	u_int32_t port;		/* Port on which to connect to this site. */
} repsite_t;

/* Globals */
extern gint master_eid;
extern gchar *myaddr;

struct __member;	typedef struct __member member_t;
struct __machtab;	typedef struct __machtab machtab_t;

/* Arguments for the connect_all thread. */
typedef struct {
	DB_ENV *dbenv;
	const gchar *progname;
	const gchar *home;
	machtab_t *machtab;
	repsite_t *sites;
	gint nsites;
} all_args;

/* Arguments for the connect_loop thread. */
typedef struct {
	DB_ENV *dbenv;
	const gchar * home;
	const gchar * progname;
	machtab_t *machtab;
	gint port;
} connect_args;

#define	CACHESIZE	(10 * 1024 * 1024)
#define	DATABASE	"quote.db"
#define	SLEEPTIME	3

gpointer connect_all __P((gpointer args));
gpointer connect_thread __P((gpointer args));
gint doclient __P((DB_ENV *, const gchar *, machtab_t *));
gint domaster __P((DB_ENV *, const gchar *));
gint get_accepted_socket __P((const gchar *, int));
gint get_connected_socket __P((machtab_t *, const gchar *, const gchar *, int, gint *, gint *));
gint get_next_message __P((int, DBT *, DBT *));
gint listen_socket_init __P((const gchar *, int));
gint listen_socket_accept __P((machtab_t *, const gchar *, int, gint *));
gint machtab_getinfo __P((machtab_t *, int, u_int32_t *, gint *));
gint machtab_init __P((machtab_t **, int, int));
void machtab_parm __P((machtab_t *, gint *, gint *, u_int32_t *));
gint machtab_rem __P((machtab_t *, int, int));
gint quote_send __P((DB_ENV *, const DBT *, const DBT *, int, u_int32_t));

#ifndef COMPQUIET
#define	COMPQUIET(x,y)	x = (y)
#endif

#endif /* !_EX_REPQUOTE_H_ */
