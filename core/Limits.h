/*
 * BDB card database - core
 * All limits in one place (spec v0.4, 5.6; I/O spec 3.2, 3.6).
 */
#ifndef BDB_LIMITS_H
#define BDB_LIMITS_H

#include <cstddef>
#include <cstdint>


namespace bdb {


static const int kFormatVersion = 1;

static const size_t kMaxManifestBytes = 64 * 1024;
static const size_t kMaxTemplateBytes = 256 * 1024;
static const size_t kMaxCardBytes = 4 * 1024 * 1024;
static const int kMaxJsonDepth = 32;
static const size_t kMaxParts = 256;
static const size_t kMaxPartNameChars = 64;

static const int32_t kMinCardSize = 64;
static const int32_t kMaxCardSize = 8192;
static const int32_t kDefaultCardWidth = 512;
static const int32_t kDefaultCardHeight = 342;

static const double kMaxNumberMagnitude = 1e15;
static const int kMaxDecimals = 6;

static const int64_t kSeqStep = 1024;

static const size_t kMaxTitleChars = 40;
static const size_t kMaxTitleBytes = 120;

static const int kCardScanDepth = 2;

static const size_t kMaxThemeBytes = 64 * 1024;
// Media files copied into a stack in memory (icons, I/O dedupe).
static const size_t kMaxIconBytes = 1024 * 1024;

// Links (I/O spec 2.2)
static const size_t kMaxUrlLength = 2048;

// CSV import (I/O spec 3.2)
static const uint64_t kMaxCsvBytes = 512ULL * 1024 * 1024;
static const size_t kMaxCsvRows = 1000000;
static const size_t kMaxCsvFieldBytes = 1024 * 1024;
static const size_t kCsvInferRows = 1000;
static const size_t kCsvLongText = 80;

// Image fetching (I/O spec 3.6)
static const int kFetchTimeoutSeconds = 15;
static const int kFetchMaxRedirects = 5;
static const size_t kFetchMaxBytes = 20 * 1024 * 1024;
static const int kFetchConnections = 4;
static const int kFetchConnectionsPerHost = 2;
static const int kFetchRetries = 1;
static const int kDefaultThumbnailEdge = 256;

// Log (21.7)
static const size_t kLogRotateBytes = 10 * 1024 * 1024;
static const size_t kLogStackTotalBytes = 100 * 1024 * 1024;
static const int kLogRetentionMonths = 12;


}	// namespace bdb

#endif	// BDB_LIMITS_H
