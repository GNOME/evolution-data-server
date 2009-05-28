/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2002
 *	Sleepycat Software.  All rights reserved.
 *
 * $Id$
 */

#ifndef _EX_APPREC_H_
#define	_EX_APPREC_H_

#include "ex_apprec_auto.h"

gint ex_apprec_mkdir_log
    __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, const DBT *));
gint ex_apprec_mkdir_print
    __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint ex_apprec_mkdir_read
    __P((DB_ENV *, gpointer , ex_apprec_mkdir_args **));
gint ex_apprec_mkdir_recover
    __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));

#endif /* !_EX_APPREC_H_ */
