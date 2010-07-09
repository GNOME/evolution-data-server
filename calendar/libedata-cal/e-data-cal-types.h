
#ifndef __E_DATA_CAL_TYPES_H__
#define __E_DATA_CAL_TYPES_H__

G_BEGIN_DECLS

typedef enum {
	Success,
	RepositoryOffline,
	PermissionDenied,
	InvalidRange,
	ObjectNotFound,
	InvalidObject,
	ObjectIdAlreadyExists,
	AuthenticationFailed,
	AuthenticationRequired,
	UnsupportedField,
	UnsupportedMethod,
	UnsupportedAuthenticationMethod,
	TLSNotAvailable,
	NoSuchCal,
	UnknownUser,
	OfflineUnavailable,

	/* These can be returned for successful searches, but
		indicate the result set was truncated */
	SearchSizeLimitExceeded,
	SearchTimeLimitExceeded,

	InvalidQuery,
	QueryRefused,

	CouldNotCancel,

	OtherError,
	InvalidServerVersion,
	InvalidArg,
	NotSupported
} EDataCalCallStatus;

typedef enum {
	ModeSet,                    /* All OK */
	ModeNotSet,                /* Generic error */
	ModeNotSupported           /* Mode not supported */
} EDataCalViewListenerSetModeStatus;

typedef enum {
	Event = 1 << 0,
	Todo = 1 << 1,
	Journal = 1 << 2,
	AnyType = 0x07
} EDataCalObjType;

typedef enum {
	This = 1 << 0,
	ThisAndPrior = 1 << 1,
	ThisAndFuture = 1 << 2,
	All = 0x07
} EDataCalObjModType;

typedef enum {
	Local = 1 << 0,
	Remote = 1 << 1,
	AnyMode = 0x07
} EDataCalMode;

G_END_DECLS

#endif /* __E_DATA_CAL_TYPES_H__ */
