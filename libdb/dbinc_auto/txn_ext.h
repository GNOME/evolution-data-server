/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_txn_ext_h_
#define	_txn_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __txn_begin __P((DB_ENV *, DB_TXN *, DB_TXN **, u_int32_t));
gint __txn_xa_begin __P((DB_ENV *, DB_TXN *));
gint __txn_compensate_begin __P((DB_ENV *, DB_TXN **txnp));
gint __txn_commit __P((DB_TXN *, u_int32_t));
gint __txn_abort __P((DB_TXN *));
gint __txn_discard __P((DB_TXN *, u_int32_t flags));
gint __txn_prepare __P((DB_TXN *, u_int8_t *));
u_int32_t __txn_id __P((DB_TXN *));
gint __txn_checkpoint __P((DB_ENV *, u_int32_t, u_int32_t, u_int32_t));
gint __txn_getckp __P((DB_ENV *, DB_LSN *));
gint __txn_activekids __P((DB_ENV *, u_int32_t, DB_TXN *));
gint __txn_force_abort __P((DB_ENV *, u_int8_t *));
gint __txn_preclose __P((DB_ENV *));
gint __txn_reset __P((DB_ENV *));
void __txn_updateckp __P((DB_ENV *, DB_LSN *));
gint __txn_regop_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, u_int32_t, int32_t));
gint __txn_regop_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_regop_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_regop_read __P((DB_ENV *, gpointer , __txn_regop_args **));
gint __txn_ckp_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, DB_LSN *, DB_LSN *, int32_t));
gint __txn_ckp_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_ckp_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_ckp_read __P((DB_ENV *, gpointer , __txn_ckp_args **));
gint __txn_child_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, u_int32_t, DB_LSN *));
gint __txn_child_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_child_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_child_read __P((DB_ENV *, gpointer , __txn_child_args **));
gint __txn_xa_regop_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, u_int32_t, const DBT *, int32_t, u_int32_t, u_int32_t, DB_LSN *));
gint __txn_xa_regop_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_xa_regop_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_xa_regop_read __P((DB_ENV *, gpointer , __txn_xa_regop_args **));
gint __txn_recycle_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, u_int32_t, u_int32_t));
gint __txn_recycle_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_recycle_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_recycle_read __P((DB_ENV *, gpointer , __txn_recycle_args **));
gint __txn_init_print __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __txn_init_getpgnos __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __txn_init_recover __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
void __txn_dbenv_create __P((DB_ENV *));
gint __txn_regop_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_xa_regop_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_ckp_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_child_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __txn_restore_txn __P((DB_ENV *, DB_LSN *, __txn_xa_regop_args *));
gint __txn_recycle_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
void __txn_continue __P((DB_ENV *, DB_TXN *, TXN_DETAIL *, size_t));
gint __txn_map_gid __P((DB_ENV *, u_int8_t *, TXN_DETAIL **, size_t *));
gint __txn_recover __P((DB_ENV *, DB_PREPLIST *, long, long *, u_int32_t));
gint __txn_get_prepared __P((DB_ENV *, XID *, DB_PREPLIST *, long, long *, u_int32_t));
gint __txn_open __P((DB_ENV *));
gint __txn_dbenv_refresh __P((DB_ENV *));
void __txn_region_destroy __P((DB_ENV *, REGINFO *));
gint __txn_id_set __P((DB_ENV *, u_int32_t, u_int32_t));
gint __txn_stat __P((DB_ENV *, DB_TXN_STAT **, u_int32_t));
gint __txn_remevent __P((DB_ENV *, DB_TXN *, const gchar *, u_int8_t*));
gint __txn_lockevent __P((DB_ENV *, DB_TXN *, DB *, DB_LOCK *, u_int32_t));
void __txn_remlock __P((DB_ENV *, DB_TXN *, DB_LOCK *, u_int32_t));
gint __txn_doevents __P((DB_ENV *, DB_TXN *, int, int));

#if defined(__cplusplus)
}
#endif
#endif /* !_txn_ext_h_ */
