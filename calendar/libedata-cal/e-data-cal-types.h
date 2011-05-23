
#ifndef __E_DATA_CAL_TYPES_H__
#define __E_DATA_CAL_TYPES_H__

G_BEGIN_DECLS

typedef enum {
	Success,
	Busy,
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

G_END_DECLS

#endif /* __E_DATA_CAL_TYPES_H__ */
