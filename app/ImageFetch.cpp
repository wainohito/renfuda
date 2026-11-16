/*
 * BDB card database - RenkuOS application
 */
#include "ImageFetch.h"

#include <Autolock.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <DataIO.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <Url.h>

#include <HttpRequest.h>
#include <HttpResult.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>

#include <algorithm>
#include <cstring>

#include "Constants.h"
#include "Limits.h"
#include "Link.h"
#include "Store.h"


using namespace BPrivate::Network;


namespace {


// Collects the response, refusing more than the size limit.
class LimitedOutput : public BDataIO {
public:
	LimitedOutput(std::string& data)
		:
		fData(data),
		fTooLarge(false)
	{
	}

	virtual ssize_t Write(const void* buffer, size_t size)
	{
		if (fData.size() + size > bdb::kFetchMaxBytes) {
			fTooLarge = true;
			return B_NO_MEMORY;
		}
		fData.append(static_cast<const char*>(buffer), size);
		return ssize_t(size);
	}

	bool TooLarge() const { return fTooLarge; }

private:
	std::string&	fData;
	bool			fTooLarge;
};


}	// namespace


static bool
FetchOnce(const std::string& url, std::string& outData, std::string& reason,
	const volatile bool* cancel)
{
	outData.clear();
	BUrl parsed(url.c_str(), true);
	if (!parsed.IsValid()) {
		reason = "invalid_url";
		return false;
	}
	LimitedOutput output(outData);
	BUrlRequest* request = BUrlProtocolRoster::MakeRequest(parsed, &output,
		NULL, NULL);
	if (request == NULL) {
		reason = "scheme";
		return false;
	}
	BHttpRequest* http = dynamic_cast<BHttpRequest*>(request);
	if (http == NULL) {
		delete request;
		reason = "scheme";
		return false;
	}
	BString agent("Renfuda/");
	agent << BDB_VERSION;
	http->SetUserAgent(agent);
	http->SetFollowLocation(true);
	http->SetMaxRedirections(bdb::kFetchMaxRedirects);
	http->SetTimeout(bdb::kFetchTimeoutSeconds * 1000000LL);
	http->SetAutoReferrer(false);

	bigtime_t start = system_time();
	thread_id thread = request->Run();
	if (thread < 0) {
		delete request;
		reason = "connect";
		return false;
	}
	bool timedOut = false;
	bool canceled = false;
	// The whole download may take a little longer than the connection
	// timeout, but not forever.
	bigtime_t limit = bdb::kFetchTimeoutSeconds * 4 * 1000000LL;
	while (request->IsRunning()) {
		if (cancel != NULL && *cancel) {
			canceled = true;
			request->Stop();
			break;
		}
		if (system_time() - start > limit) {
			timedOut = true;
			request->Stop();
			break;
		}
		snooze(50000);
	}
	status_t exitValue;
	wait_for_thread(thread, &exitValue);

	const BHttpResult& result = dynamic_cast<const BHttpResult&>(
		request->Result());
	int32 code = result.StatusCode();
	BString type = result.ContentType();
	status_t status = request->Status();
	delete request;

	if (canceled) {
		reason = "canceled";
		return false;
	}
	if (output.TooLarge()) {
		reason = "too_large";
		return false;
	}
	if (timedOut || status == B_TIMED_OUT) {
		reason = "timeout";
		return false;
	}
	if (code == 0 || status != B_OK) {
		reason = "connect";
		return false;
	}
	if (code < 200 || code >= 300) {
		reason = "http_" + std::to_string(code);
		return false;
	}
	// The response decides, not the URL (I/O 3.6).
	if (!type.IStartsWith("image/")) {
		reason = "not_image";
		return false;
	}
	return true;
}


bool
FetchUrl(const std::string& url, std::string& outData, std::string& reason,
	const volatile bool* cancel)
{
	std::string scheme;
	if (bdb::CheckUrl(url, &scheme) != bdb::LinkCheck::kOk
		|| (scheme != "http" && scheme != "https")) {
		reason = "invalid_url";
		return false;
	}
	for (int attempt = 0; attempt <= bdb::kFetchRetries; attempt++) {
		if (FetchOnce(url, outData, reason, cancel))
			return true;
		// Retry only what may go away (I/O 3.6: one retry).
		if (reason != "connect" && reason != "timeout"
			&& reason.compare(0, 6, "http_5") != 0) {
			break;
		}
	}
	return false;
}


bool
MakeThumbnail(const std::string& data, int maxEdge, std::string& out,
	std::string& outExt, std::string& reason)
{
	BMemoryIO input(data.data(), data.size());
	BBitmap* source = BTranslationUtils::GetBitmap(&input);
	if (source == NULL) {
		reason = "unreadable";
		return false;
	}
	// Work on B_RGBA32 pixels. Color spaces without alpha are opaque (the
	// alpha byte of B_RGB32 is undefined).
	color_space space = source->ColorSpace();
	bool sourceAlpha = space == B_RGBA32 || space == B_RGBA15
		|| space == B_CMAP8;
	if (space != B_RGB32 && space != B_RGBA32) {
		BBitmap* converted = new BBitmap(source->Bounds(), 0, B_RGBA32);
		if (converted->ImportBits(source) != B_OK) {
			delete converted;
			delete source;
			reason = "unreadable";
			return false;
		}
		delete source;
		source = converted;
	}
	int32 width = source->Bounds().IntegerWidth() + 1;
	int32 height = source->Bounds().IntegerHeight() + 1;
	float scale = std::min(1.0f,
		float(maxEdge) / float(std::max(width, height)));
	int32 targetWidth = std::max(1, int(width * scale + 0.5f));
	int32 targetHeight = std::max(1, int(height * scale + 0.5f));

	// Scaled down in software (box filter, alpha-weighted): drawing into
	// an offscreen bitmap would leave its alpha channel untouched.
	BBitmap* target = new BBitmap(BRect(0, 0, targetWidth - 1,
		targetHeight - 1), 0, B_RGBA32);
	const uint8* sourceBits = static_cast<const uint8*>(source->Bits());
	int32 sourceRow = source->BytesPerRow();
	uint8* targetBits = static_cast<uint8*>(target->Bits());
	int32 targetRow = target->BytesPerRow();
	for (int32 ty = 0; ty < targetHeight; ty++) {
		int32 y0 = int32(int64(ty) * height / targetHeight);
		int32 y1 = std::max(y0 + 1,
			int32(int64(ty + 1) * height / targetHeight));
		for (int32 tx = 0; tx < targetWidth; tx++) {
			int32 x0 = int32(int64(tx) * width / targetWidth);
			int32 x1 = std::max(x0 + 1,
				int32(int64(tx + 1) * width / targetWidth));
			uint64 sum[3] = { 0, 0, 0 };
			uint64 alphaSum = 0;
			for (int32 y = y0; y < y1; y++) {
				const uint8* pixel = sourceBits + y * sourceRow + x0 * 4;
				for (int32 x = x0; x < x1; x++, pixel += 4) {
					uint32 alpha = sourceAlpha ? pixel[3] : 255;
					sum[0] += pixel[0] * alpha;
					sum[1] += pixel[1] * alpha;
					sum[2] += pixel[2] * alpha;
					alphaSum += alpha;
				}
			}
			uint8* out = targetBits + ty * targetRow + tx * 4;
			uint64 count = uint64(x1 - x0) * (y1 - y0);
			for (int i = 0; i < 3; i++)
				out[i] = alphaSum == 0 ? 0 : uint8(sum[i] / alphaSum);
			out[3] = uint8((alphaSum + count / 2) / count);
		}
	}
	delete source;

	// Transparent pixels: PNG; otherwise JPEG (I/O 3.6).
	bool transparent = false;
	const uint8* bits = static_cast<const uint8*>(target->Bits());
	int32 rowBytes = target->BytesPerRow();
	for (int32 y = 0; y < targetHeight && !transparent; y++) {
		const uint8* row = bits + y * rowBytes;
		for (int32 x = 0; x < targetWidth; x++) {
			if (row[x * 4 + 3] != 255) {
				transparent = true;
				break;
			}
		}
	}
	BBitmapStream stream(target);
	BMallocIO output;
	BMessage settings;
	settings.AddInt32("quality", 85);
	status_t status = BTranslatorRoster::Default()->Translate(&stream, NULL,
		&settings, &output, transparent ? B_PNG_FORMAT : B_JPEG_FORMAT);
	BBitmap* detached = NULL;
	stream.DetachBitmap(&detached);
	delete detached;
	if (status != B_OK) {
		reason = "encode";
		return false;
	}
	out.assign(static_cast<const char*>(output.Buffer()),
		size_t(output.BufferLength()));
	outExt = transparent ? "png" : "jpg";
	return true;
}


// #pragma mark - ImageFetcher


static std::string
HostOf(const std::string& url)
{
	BUrl parsed(url.c_str(), true);
	return parsed.IsValid() ? std::string(parsed.Host().String())
		: std::string();
}


ImageFetcher::ImageFetcher(BMessenger target,
	const std::vector<bdb::FetchItem>& items)
	:
	fTarget(target),
	fItems(items),
	fNext(0),
	fDone(0),
	fRunning(0),
	fCanceled(false),
	fLock("image fetcher")
{
}


ImageFetcher::~ImageFetcher()
{
	Cancel();
	for (thread_id thread : fThreads) {
		status_t result;
		wait_for_thread(thread, &result);
	}
}


void
ImageFetcher::Start()
{
	int32 count = std::min(bdb::kFetchConnections, int(fItems.size()));
	fRunning = count;
	for (int32 i = 0; i < count; i++) {
		thread_id thread = spawn_thread(&_Worker, "bdb image fetch",
			B_LOW_PRIORITY, this);
		if (thread < 0) {
			fRunning--;
			continue;
		}
		fThreads.push_back(thread);
		resume_thread(thread);
	}
	if (fRunning == 0) {
		BMessage message(kMsgFetchProgress);
		message.AddInt32("done", 0);
		message.AddInt32("total", int32(fItems.size()));
		message.AddBool("finished", true);
		fTarget.SendMessage(&message);
	}
}


void
ImageFetcher::Cancel()
{
	fCanceled = true;
}


bool
ImageFetcher::_Next(bdb::FetchItem& item, std::string& host)
{
	while (!fCanceled) {
		{
			BAutolock lock(fLock);
			if (fNext >= fItems.size())
				return false;
			// The first waiting item whose host has a free connection
			// (at most 2 per host).
			for (size_t i = fNext; i < fItems.size(); i++) {
				std::string candidate = HostOf(fItems[i].url);
				if (fPerHost[candidate] >= bdb::kFetchConnectionsPerHost)
					continue;
				std::swap(fItems[i], fItems[fNext]);
				item = fItems[fNext++];
				host = candidate;
				fPerHost[host]++;
				return true;
			}
		}
		snooze(50000);
	}
	return false;
}


void
ImageFetcher::_Done(const std::string& host)
{
	BAutolock lock(fLock);
	fPerHost[host]--;
	fDone++;
}


status_t
ImageFetcher::_Worker(void* data)
{
	ImageFetcher* self = static_cast<ImageFetcher*>(data);
	bdb::FetchItem item;
	std::string host;
	while (self->_Next(item, host)) {
		std::string bytes, reason, thumbnail, ext;
		bool ok = FetchUrl(item.url, bytes, reason, &self->fCanceled)
			&& MakeThumbnail(bytes, item.maxEdge, thumbnail, ext, reason);
		if (reason == "unreadable")
			reason = "not_image";
		self->_Done(host);
		if (self->fCanceled)
			break;
		BMessage result(kMsgFetchResult);
		result.AddString("card", item.cardId.c_str());
		result.AddString("part", item.partId.c_str());
		result.AddBool("ok", ok);
		if (ok) {
			result.AddData("data", B_RAW_TYPE, thumbnail.data(),
				thumbnail.size());
			result.AddString("ext", ext.c_str());
		} else
			result.AddString("reason", reason.c_str());
		self->fTarget.SendMessage(&result);
		BMessage progress(kMsgFetchProgress);
		progress.AddInt32("done", self->fDone);
		progress.AddInt32("total", int32(self->fItems.size()));
		self->fTarget.SendMessage(&progress);
	}
	bool last;
	{
		BAutolock lock(self->fLock);
		last = --self->fRunning == 0;
	}
	if (last) {
		BMessage progress(kMsgFetchProgress);
		progress.AddInt32("done", self->fDone);
		progress.AddInt32("total", int32(self->fItems.size()));
		progress.AddBool("finished", true);
		self->fTarget.SendMessage(&progress);
	}
	return B_OK;
}


// #pragma mark - AppImageSource


bool
AppImageSource::FromFile(const std::string& path, int maxEdge,
	std::string& outData, std::string& outExt, std::string& reason)
{
	std::string data;
	bdb::Status status = bdb::ReadWholeFile(path, bdb::kFetchMaxBytes, data);
	if (!status) {
		reason = status.Kind() == bdb::ErrorKind::kLimitExceeded
			? "too_large" : "not_found";
		return false;
	}
	return MakeThumbnail(data, maxEdge, outData, outExt, reason);
}


bool
AppImageSource::FromUrl(const std::string& url, int maxEdge,
	std::string& outData, std::string& outExt, std::string& reason)
{
	std::string data;
	if (!FetchUrl(url, data, reason, fCancel))
		return false;
	if (!MakeThumbnail(data, maxEdge, outData, outExt, reason)) {
		reason = "not_image";
		return false;
	}
	return true;
}


// #pragma mark - FetchItems


namespace {


struct FetchPool {
	const std::vector<bdb::FetchItem>*	items;
	const volatile bool*	cancel;
	const FetchCallback*	done;
	BLocker					lock;
	BLocker					callbackLock;
	size_t					next = 0;
	std::vector<size_t>		order;
	std::map<std::string, int> perHost;

	FetchPool() : lock("fetch pool"), callbackLock("fetch callback") {}

	bool Next(size_t& index, std::string& host)
	{
		while (cancel == NULL || !*cancel) {
			{
				BAutolock locker(lock);
				if (next >= order.size())
					return false;
				for (size_t i = next; i < order.size(); i++) {
					std::string candidate = HostOf((*items)[order[i]].url);
					if (perHost[candidate] >= bdb::kFetchConnectionsPerHost)
						continue;
					std::swap(order[i], order[next]);
					index = order[next++];
					host = candidate;
					perHost[host]++;
					return true;
				}
			}
			snooze(50000);
		}
		return false;
	}
};


status_t
PoolWorker(void* data)
{
	FetchPool* pool = static_cast<FetchPool*>(data);
	size_t index;
	std::string host;
	while (pool->Next(index, host)) {
		const bdb::FetchItem& item = (*pool->items)[index];
		std::string bytes, reason, thumbnail, ext;
		bool ok = FetchUrl(item.url, bytes, reason, pool->cancel)
			&& MakeThumbnail(bytes, item.maxEdge, thumbnail, ext, reason);
		if (reason == "unreadable")
			reason = "not_image";
		{
			BAutolock locker(pool->lock);
			pool->perHost[host]--;
		}
		if (pool->cancel != NULL && *pool->cancel)
			break;
		BAutolock locker(pool->callbackLock);
		(*pool->done)(item, ok, thumbnail, ext, reason);
	}
	return B_OK;
}


}	// namespace


void
FetchItems(const std::vector<bdb::FetchItem>& items,
	const volatile bool* cancel, const FetchCallback& done)
{
	FetchPool pool;
	pool.items = &items;
	pool.cancel = cancel;
	pool.done = &done;
	for (size_t i = 0; i < items.size(); i++)
		pool.order.push_back(i);
	std::vector<thread_id> threads;
	int count = std::min(bdb::kFetchConnections, int(items.size()));
	for (int i = 0; i < count; i++) {
		thread_id thread = spawn_thread(&PoolWorker, "bdb fetch",
			B_LOW_PRIORITY, &pool);
		if (thread >= 0) {
			threads.push_back(thread);
			resume_thread(thread);
		}
	}
	for (thread_id thread : threads) {
		status_t result;
		wait_for_thread(thread, &result);
	}
}
