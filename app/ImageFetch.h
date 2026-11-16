/*
 * BDB card database - RenkuOS application
 * Fetching images over HTTP and making thumbnails (I/O spec v0.1, 3.6).
 *
 * - http and https only; timeout 15 s, at most 5 redirects, 20 MiB, one
 *   retry; User-Agent "BDB/<version>".
 * - Whether a response is an image is decided by its Content-Type
 *   (image/...) and by decoding it, not by the URL.
 * - Thumbnails: the long edge scaled down to max_edge (never up), the
 *   EXIF orientation applied (the JPEG translator does this), stored as
 *   JPEG (quality 85) or PNG when the image has transparency. The
 *   original is not kept.
 * - At most 4 downloads at a time, 2 per host.
 * - Never logs URLs: failures carry a short reason code only.
 */
#ifndef BDB_IMAGE_FETCH_H
#define BDB_IMAGE_FETCH_H

#include <Locker.h>
#include <Messenger.h>
#include <OS.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Csv.h"


// Downloads a URL. `reason`: invalid_url, scheme, connect, timeout,
// http_<status>, not_image, too_large, canceled.
bool FetchUrl(const std::string& url, std::string& outData,
	std::string& reason, const volatile bool* cancel = NULL);

// Decodes `data` (any format the Translation Kit reads) and encodes the
// thumbnail. `reason`: unreadable.
bool MakeThumbnail(const std::string& data, int maxEdge, std::string& out,
	std::string& outExt, std::string& reason);


// Fetches the items with up to 4 threads (2 per host) and calls `done`
// for each, one call at a time, in the calling thread's context of
// work (FetchItems returns when all are done or `cancel` is set).
typedef std::function<void(const bdb::FetchItem& item, bool ok,
	const std::string& data, const std::string& ext,
	const std::string& reason)> FetchCallback;
void FetchItems(const std::vector<bdb::FetchItem>& items,
	const volatile bool* cancel, const FetchCallback& done);


// Fetches FetchItems in the background and posts to `target`:
//   kMsgFetchResult: "card", "part", "ok" (bool), "data" (raw), "ext",
//                    "reason"
//   kMsgFetchProgress: "done", "total", "finished" (bool, last message)
class ImageFetcher {
public:
								ImageFetcher(BMessenger target,
									const std::vector<bdb::FetchItem>& items);
								~ImageFetcher();

			void				Start();
	// Stops after the downloads that are running.
			void				Cancel();

private:
	static	status_t			_Worker(void* data);
			bool				_Next(bdb::FetchItem& item,
									std::string& host);
			void				_Done(const std::string& host);

			BMessenger			fTarget;
			std::vector<bdb::FetchItem> fItems;
			size_t				fNext;
			int32				fDone;
			int32				fRunning;
			volatile bool		fCanceled;
			BLocker				fLock;
			std::map<std::string, int> fPerHost;
			std::vector<thread_id> fThreads;
};


// Images for the CSV import (synchronous; called from the import thread).
class AppImageSource : public bdb::CsvImageSource {
public:
								AppImageSource(const volatile bool* cancel)
									: fCancel(cancel) {}
	virtual	bool				FromFile(const std::string& path,
									int maxEdge, std::string& outData,
									std::string& outExt,
									std::string& reason);
	virtual	bool				FromUrl(const std::string& url, int maxEdge,
									std::string& outData,
									std::string& outExt,
									std::string& reason);

private:
			const volatile bool* fCancel;
};


#endif	// BDB_IMAGE_FETCH_H
