/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __EXCHANGE_OOF_H__
#define __EXCHANGE_OOF_H__

#include "exchange-types.h"
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

gboolean      exchange_oof_get              (ExchangeAccount  *account,
					     gboolean         *oof,
					     char            **mmsg);
gboolean      exchange_oof_set              (ExchangeAccount  *account,
					     gboolean          oof,
					     const char       *msg);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_OOF_H__ */
