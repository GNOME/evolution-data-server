/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_mp_ext_h_
#define	_mp_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __memp_alloc __P((DB_MPOOL *, REGINFO *, MPOOLFILE *, size_t, roff_t *, gpointer ));
#ifdef DIAGNOSTIC
void __memp_check_order __P((DB_MPOOL_HASH *));
#endif
gint __memp_bhwrite __P((DB_MPOOL *, DB_MPOOL_HASH *, MPOOLFILE *, BH *, int));
gint __memp_pgread __P((DB_MPOOLFILE *, DB_MUTEX *, BH *, int));
gint __memp_pg __P((DB_MPOOLFILE *, BH *, int));
void __memp_bhfree __P((DB_MPOOL *, DB_MPOOL_HASH *, BH *, int));
gint __memp_fget __P((DB_MPOOLFILE *, db_pgno_t *, u_int32_t, gpointer ));
gint __memp_fcreate __P((DB_ENV *, DB_MPOOLFILE **, u_int32_t));
gint __memp_fopen_int __P((DB_MPOOLFILE *, MPOOLFILE *, const gchar *, u_int32_t, int, size_t));
gint __memp_fclose_int __P((DB_MPOOLFILE *, u_int32_t));
gint __memp_mf_discard __P((DB_MPOOL *, MPOOLFILE *));
gchar * __memp_fn __P((DB_MPOOLFILE *));
gchar * __memp_fns __P((DB_MPOOL *, MPOOLFILE *));
gint __memp_fput __P((DB_MPOOLFILE *, gpointer , u_int32_t));
gint __memp_fset __P((DB_MPOOLFILE *, gpointer , u_int32_t));
void __memp_dbenv_create __P((DB_ENV *));
gint __memp_open __P((DB_ENV *));
gint __memp_dbenv_refresh __P((DB_ENV *));
void __mpool_region_destroy __P((DB_ENV *, REGINFO *));
gint  __memp_nameop __P((DB_ENV *, u_int8_t *, const gchar *, const gchar *, const gchar *));
gint __memp_register __P((DB_ENV *, int, gint (*)(DB_ENV *, db_pgno_t, gpointer , DBT *), gint (*)(DB_ENV *, db_pgno_t, gpointer , DBT *)));
gint __memp_stat __P((DB_ENV *, DB_MPOOL_STAT **, DB_MPOOL_FSTAT ***, u_int32_t));
gint __memp_dump_region __P((DB_ENV *, gchar *, FILE *));
void __memp_stat_hash __P((REGINFO *, MPOOL *, u_int32_t *));
gint __memp_sync __P((DB_ENV *, DB_LSN *));
gint __memp_fsync __P((DB_MPOOLFILE *));
gint __mp_xxx_fh __P((DB_MPOOLFILE *, DB_FH **));
gint __memp_sync_int __P((DB_ENV *, DB_MPOOLFILE *, int, db_sync_op, gint *));
gint __memp_trickle __P((DB_ENV *, int, gint *));

#if defined(__cplusplus)
}
#endif
#endif /* !_mp_ext_h_ */
