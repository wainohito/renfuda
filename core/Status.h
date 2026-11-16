/*
 * BDB card database - core
 * Typed result values (spec v0.2, chapter 17 "エラー").
 */
#ifndef BDB_STATUS_H
#define BDB_STATUS_H

#include <string>


namespace bdb {


enum class ErrorKind {
	kOk = 0,
	kNotFound,
	kInvalidFormat,
	kLimitExceeded,
	kConflict,
	kWriteFailed,
	kReadFailed,
	kExists,
	kUnsupported,		// encrypted stack / encrypted file
	kNewerVersion,		// format_version newer than this implementation
	kInvalidArgument
};


class Status {
public:
						Status() : fKind(ErrorKind::kOk) {}
						Status(ErrorKind kind, std::string detail = "")
							: fKind(kind), fDetail(std::move(detail)) {}

	static	Status		Ok() { return Status(); }

			bool		IsOk() const { return fKind == ErrorKind::kOk; }
			explicit	operator bool() const { return IsOk(); }
			ErrorKind	Kind() const { return fKind; }
	const	std::string& Detail() const { return fDetail; }

	// Short machine-readable code, used as "err" in the log (21.5).
	const	char*		Code() const { return CodeFor(fKind); }
	static	const char*	CodeFor(ErrorKind kind);

private:
			ErrorKind	fKind;
			std::string	fDetail;
};


inline const char*
Status::CodeFor(ErrorKind kind)
{
	switch (kind) {
		case ErrorKind::kOk:				return "ok";
		case ErrorKind::kNotFound:			return "not_found";
		case ErrorKind::kInvalidFormat:		return "invalid_format";
		case ErrorKind::kLimitExceeded:		return "limit_exceeded";
		case ErrorKind::kConflict:			return "conflict";
		case ErrorKind::kWriteFailed:		return "write_failed";
		case ErrorKind::kReadFailed:		return "read_failed";
		case ErrorKind::kExists:			return "exists";
		case ErrorKind::kUnsupported:		return "unsupported";
		case ErrorKind::kNewerVersion:		return "newer_version";
		case ErrorKind::kInvalidArgument:	return "invalid_argument";
	}
	return "unknown";
}


}	// namespace bdb

#endif	// BDB_STATUS_H
