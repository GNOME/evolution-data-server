/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_btree_ext_h_
#define	_btree_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __bam_cmp __P((DB *, const DBT *, PAGE *, u_int32_t, gint (*)(DB *, const DBT *, const DBT *), gint *));
gint __bam_defcmp __P((DB *, const DBT *, const DBT *));
size_t __bam_defpfx __P((DB *, const DBT *, const DBT *));
gint __bam_pgin __P((DB_ENV *, DB *, db_pgno_t, gpointer , DBT *));
gint __bam_pgout __P((DB_ENV *, DB *, db_pgno_t, gpointer , DBT *));
gint __bam_mswap __P((PAGE *));
void __bam_cprint __P((DBC *));
gint __bam_ca_delete __P((DB *, db_pgno_t, u_int32_t, int));
gint __ram_ca_delete __P((DB *, db_pgno_t));
gint __bam_ca_di __P((DBC *, db_pgno_t, u_int32_t, int));
gint __bam_ca_dup __P((DBC *, u_int32_t, db_pgno_t, u_int32_t, db_pgno_t, u_int32_t));
gint __bam_ca_undodup __P((DB *, u_int32_t, db_pgno_t, u_int32_t, u_int32_t));
gint __bam_ca_rsplit __P((DBC *, db_pgno_t, db_pgno_t));
gint __bam_ca_split __P((DBC *, db_pgno_t, db_pgno_t, db_pgno_t, u_int32_t, int));
void __bam_ca_undosplit __P((DB *, db_pgno_t, db_pgno_t, db_pgno_t, u_int32_t));
gint __bam_c_init __P((DBC *, DBTYPE));
gint __bam_c_refresh __P((DBC *));
gint __bam_c_count __P((DBC *, db_recno_t *));
gint __bam_c_dup __P((DBC *, DBC *));
gint __bam_bulk_overflow __P((DBC *, u_int32_t, db_pgno_t, u_int8_t *));
gint __bam_bulk_duplicates __P((DBC *, db_pgno_t, u_int8_t *, int32_t *, int32_t **, u_int8_t **, u_int32_t *, int));
gint __bam_c_rget __P((DBC *, DBT *));
gint __bam_ditem __P((DBC *, PAGE *, u_int32_t));
gint __bam_adjindx __P((DBC *, PAGE *, u_int32_t, u_int32_t, int));
gint __bam_dpages __P((DBC *, EPG *));
gint __bam_db_create __P((DB *));
gint __bam_db_close __P((DB *));
gint __bam_set_flags __P((DB *, u_int32_t *flagsp));
gint __ram_set_flags __P((DB *, u_int32_t *flagsp));
gint __bam_open __P((DB *, DB_TXN *, const gchar *, db_pgno_t, u_int32_t));
gint __bam_metachk __P((DB *, const gchar *, BTMETA *));
gint __bam_read_root __P((DB *, DB_TXN *, db_pgno_t, u_int32_t));
gint __bam_new_file __P((DB *, DB_TXN *, DB_FH *, const gchar *));
gint __bam_new_subdb __P((DB *, DB *, DB_TXN *));
gint __bam_iitem __P((DBC *, DBT *, DBT *, u_int32_t, u_int32_t));
gint __bam_ritem __P((DBC *, PAGE *, u_int32_t, DBT *));
gint __bam_split_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_rsplit_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_adj_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_cadjust_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_cdel_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_repl_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_root_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_curadj_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_rcuradj_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_reclaim __P((DB *, DB_TXN *));
gint __bam_truncate __P((DB *, DB_TXN *, u_int32_t *));
gint __ram_open __P((DB *, DB_TXN *, const gchar *, db_pgno_t, u_int32_t));
gint __ram_append __P((DBC *, DBT *, DBT *));
gint __ram_c_del __P((DBC *));
gint __ram_c_get __P((DBC *, DBT *, DBT *, u_int32_t, db_pgno_t *));
gint __ram_c_put __P((DBC *, DBT *, DBT *, u_int32_t, db_pgno_t *));
gint __ram_ca __P((DBC *, ca_recno_arg));
gint __ram_getno __P((DBC *, const DBT *, db_recno_t *, int));
gint __ram_writeback __P((DB *));
gint __bam_rsearch __P((DBC *, db_recno_t *, u_int32_t, int, gint *));
gint __bam_adjust __P((DBC *, int32_t));
gint __bam_nrecs __P((DBC *, db_recno_t *));
db_recno_t __bam_total __P((DB *, PAGE *));
gint __bam_search __P((DBC *, db_pgno_t, const DBT *, u_int32_t, int, db_recno_t *, gint *));
gint __bam_stkrel __P((DBC *, u_int32_t));
gint __bam_stkgrow __P((DB_ENV *, BTREE_CURSOR *));
gint __bam_split __P((DBC *, gpointer , db_pgno_t *));
gint __bam_copy __P((DB *, PAGE *, PAGE *, u_int32_t, u_int32_t));
gint __bam_stat __P((DB *, gpointer , u_int32_t));
gint __bam_traverse __P((DBC *, db_lockmode_t, db_pgno_t, gint (*)(DB *, PAGE *, gpointer , gint *), gpointer ));
gint __bam_stat_callback __P((DB *, PAGE *, gpointer , gint *));
gint __bam_key_range __P((DB *, DB_TXN *, DBT *, DB_KEY_RANGE *, u_int32_t));
gint __bam_30_btreemeta __P((DB *, gchar *, u_int8_t *));
gint __bam_31_btreemeta __P((DB *, gchar *, u_int32_t, DB_FH *, PAGE *, gint *));
gint __bam_31_lbtree __P((DB *, gchar *, u_int32_t, DB_FH *, PAGE *, gint *));
gint __bam_vrfy_meta __P((DB *, VRFY_DBINFO *, BTMETA *, db_pgno_t, u_int32_t));
gint __ram_vrfy_leaf __P((DB *, VRFY_DBINFO *, PAGE *, db_pgno_t, u_int32_t));
gint __bam_vrfy __P((DB *, VRFY_DBINFO *, PAGE *, db_pgno_t, u_int32_t));
gint __bam_vrfy_itemorder __P((DB *, VRFY_DBINFO *, PAGE *, db_pgno_t, u_int32_t, int, int, u_int32_t));
gint __bam_vrfy_structure __P((DB *, VRFY_DBINFO *, db_pgno_t, u_int32_t));
gint __bam_vrfy_subtree __P((DB *, VRFY_DBINFO *, db_pgno_t, gpointer , gpointer , u_int32_t, u_int32_t *, u_int32_t *, u_int32_t *));
gint __bam_salvage __P((DB *, VRFY_DBINFO *, db_pgno_t, u_int32_t, PAGE *, gpointer , gint (*)(gpointer , gconstpointer ), DBT *, u_int32_t));
gint __bam_salvage_walkdupint __P((DB *, VRFY_DBINFO *, PAGE *, DBT *, gpointer , gint (*)(gpointer , gconstpointer ), u_int32_t));
gint __bam_meta2pgset __P((DB *, VRFY_DBINFO *, BTMETA *, u_int32_t, DB *));
gint __bam_split_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, db_pgno_t, DB_LSN *, db_pgno_t, DB_LSN *, u_int32_t, db_pgno_t, DB_LSN *, db_pgno_t, const DBT *, u_int32_t));
gint __bam_split_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_split_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_split_read __P((DB_ENV *, gpointer , __bam_split_args **));
gint __bam_rsplit_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, db_pgno_t, const DBT *, db_pgno_t, db_pgno_t, const DBT *, DB_LSN *));
gint __bam_rsplit_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_rsplit_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_rsplit_read __P((DB_ENV *, gpointer , __bam_rsplit_args **));
gint __bam_adj_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, db_pgno_t, DB_LSN *, u_int32_t, u_int32_t, u_int32_t));
gint __bam_adj_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_adj_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_adj_read __P((DB_ENV *, gpointer , __bam_adj_args **));
gint __bam_cadjust_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, db_pgno_t, DB_LSN *, u_int32_t, int32_t, u_int32_t));
gint __bam_cadjust_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_cadjust_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_cadjust_read __P((DB_ENV *, gpointer , __bam_cadjust_args **));
gint __bam_cdel_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, db_pgno_t, DB_LSN *, u_int32_t));
gint __bam_cdel_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_cdel_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_cdel_read __P((DB_ENV *, gpointer , __bam_cdel_args **));
gint __bam_repl_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, db_pgno_t, DB_LSN *, u_int32_t, u_int32_t, const DBT *, const DBT *, u_int32_t, u_int32_t));
gint __bam_repl_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_repl_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_repl_read __P((DB_ENV *, gpointer , __bam_repl_args **));
gint __bam_root_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, db_pgno_t, db_pgno_t, DB_LSN *));
gint __bam_root_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_root_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_root_read __P((DB_ENV *, gpointer , __bam_root_args **));
gint __bam_curadj_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, db_ca_mode, db_pgno_t, db_pgno_t, db_pgno_t, u_int32_t, u_int32_t, u_int32_t));
gint __bam_curadj_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_curadj_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_curadj_read __P((DB_ENV *, gpointer , __bam_curadj_args **));
gint __bam_rcuradj_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, ca_recno_arg, db_pgno_t, db_recno_t, u_int32_t));
gint __bam_rcuradj_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_rcuradj_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __bam_rcuradj_read __P((DB_ENV *, gpointer , __bam_rcuradj_args **));
gint __bam_init_print __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __bam_init_getpgnos __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __bam_init_recover __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));

#if defined(__cplusplus)
}
#endif
#endif /* !_btree_ext_h_ */
