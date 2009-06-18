/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __EXCHANGE_OOF_H__
#define __EXCHANGE_OOF_H__

#include "exchange-types.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

gboolean      exchange_oof_get              (ExchangeAccount  *account,
					     gboolean         *oof,
					     gchar            **mmsg);
gboolean      exchange_oof_set              (ExchangeAccount  *account,
					     gboolean          oof,
					     const gchar       *msg);

G_END_DECLS

#endif /* __EXCHANGE_OOF_H__ */
