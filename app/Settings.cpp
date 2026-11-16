/*
 * BDB card database - RenkuOS application
 */
#include "Settings.h"

#include <Autolock.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <stdio.h>


Settings gSettings;

static const int32 kMaxRecent = 10;


static BString Key(const char* prefix, const char* stackId);


Settings::Settings()
	:
	fLock("bdb settings")
{
}


BString
Settings::SettingsDirectory()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		path.SetTo("/boot/home/config/settings");
	path.Append("Renfuda");
	create_directory(path.Path(), 0755);
	return path.Path();
}


BString
Settings::LogDirectory() const
{
	BString path = SettingsDirectory();
	path << "/logs";
	return path;
}


status_t
Settings::Load()
{
	BAutolock lock(fLock);
	BString path = SettingsDirectory();
	path << "/settings";
	BFile file(path.String(), B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	BMessage data;
	status = data.Unflatten(&file);
	if (status == B_OK)
		fData = data;
	return status;
}


status_t
Settings::Save()
{
	BAutolock lock(fLock);
	BString path = SettingsDirectory();
	BString tmpPath = path;
	path << "/settings";
	tmpPath << "/settings.tmp";
	BFile file(tmpPath.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	status = fData.Flatten(&file);
	file.Sync();
	file.Unset();
	if (status == B_OK) {
		if (rename(tmpPath.String(), path.String()) != 0)
			status = B_ERROR;
	}
	return status;
}


void
Settings::AddRecent(const char* path)
{
	BAutolock lock(fLock);
	BMessage recent;
	recent.AddString("path", path);
	int32 count = 1;
	BString existing;
	for (int32 i = 0; fData.FindString("recent", i, &existing) == B_OK; i++) {
		if (existing == path || count >= kMaxRecent)
			continue;
		recent.AddString("path", existing);
		count++;
	}
	fData.RemoveName("recent");
	for (int32 i = 0; recent.FindString("path", i, &existing) == B_OK; i++)
		fData.AddString("recent", existing);
}


int32
Settings::CountRecent()
{
	BAutolock lock(fLock);
	type_code type;
	int32 count = 0;
	if (fData.GetInfo("recent", &type, &count) != B_OK)
		return 0;
	return count;
}


BString
Settings::RecentAt(int32 index)
{
	BAutolock lock(fLock);
	BString path;
	fData.FindString("recent", index, &path);
	return path;
}


static BString
Key(const char* prefix, const char* stackId)
{
	BString key(prefix);
	key << ":" << stackId;
	return key;
}


bool
Settings::GetWindowFrame(const char* stackId, BRect& frame)
{
	BAutolock lock(fLock);
	return fData.FindRect(Key("frame", stackId).String(), &frame) == B_OK;
}


void
Settings::SetWindowFrame(const char* stackId, BRect frame)
{
	BAutolock lock(fLock);
	BString key = Key("frame", stackId);
	if (fData.ReplaceRect(key.String(), frame) != B_OK)
		fData.AddRect(key.String(), frame);
}


float
Settings::Zoom(const char* stackId)
{
	BAutolock lock(fLock);
	float zoom;
	if (fData.FindFloat(Key("zoom", stackId).String(), &zoom) != B_OK
		|| zoom < 0.1f || zoom > 8.0f) {
		return 1.0f;
	}
	return zoom;
}


bool
Settings::ZoomFit(const char* stackId)
{
	BAutolock lock(fLock);
	return fData.GetBool(Key("fit", stackId).String(), false);
}


void
Settings::SetZoom(const char* stackId, float zoom, bool fit)
{
	BAutolock lock(fLock);
	BString key = Key("zoom", stackId);
	if (fData.ReplaceFloat(key.String(), zoom) != B_OK)
		fData.AddFloat(key.String(), zoom);
	fData.SetBool(Key("fit", stackId).String(), fit);
}


bool
Settings::GetColumns(const char* key, BMessage& out)
{
	BAutolock lock(fLock);
	return fData.FindMessage(Key("columns", key).String(), &out) == B_OK;
}


void
Settings::SetColumns(const char* key, const BMessage& columns)
{
	BAutolock lock(fLock);
	BString name = Key("columns", key);
	fData.RemoveName(name.String());
	fData.AddMessage(name.String(), &columns);
}


bool
Settings::DebugLog()
{
	BAutolock lock(fLock);
	return fData.GetBool("log_debug", false);
}


void
Settings::GetRecentColors(BMessage& out)
{
	BAutolock lock(fLock);
	BString color;
	for (int32 i = 0; fData.FindString("color", i, &color) == B_OK; i++)
		out.AddString("color", color);
}


void
Settings::AddRecentColor(const char* color)
{
	BAutolock lock(fLock);
	BMessage colors;
	colors.AddString("color", color);
	BString existing;
	int32 count = 1;
	for (int32 i = 0; fData.FindString("color", i, &existing) == B_OK; i++) {
		if (existing == color || count >= 12)
			continue;
		colors.AddString("color", existing);
		count++;
	}
	fData.RemoveName("color");
	for (int32 i = 0; colors.FindString("color", i, &existing) == B_OK; i++)
		fData.AddString("color", existing);
}


bool
Settings::SamplesSeeded(const char* installStamp)
{
	BAutolock lock(fLock);
	return BString(fData.GetString("samples_seeded", "")) == installStamp;
}


void
Settings::SetSamplesSeeded(const char* installStamp)
{
	BAutolock lock(fLock);
	BString previous = fData.GetString("samples_seeded", "");
	if (previous != installStamp) {
		// A new installation: removed samples come back (UI 3.5).
		fData.RemoveName("sample_removed");
	}
	fData.RemoveName("samples_seeded");
	fData.AddString("samples_seeded", installStamp);
}


bool
Settings::IsSampleRemoved(const char* name)
{
	BAutolock lock(fLock);
	BString removed;
	for (int32 i = 0; fData.FindString("sample_removed", i, &removed) == B_OK;
			i++) {
		if (removed == name)
			return true;
	}
	return false;
}


void
Settings::RemoveSample(const char* name)
{
	if (IsSampleRemoved(name))
		return;
	BAutolock lock(fLock);
	fData.AddString("sample_removed", name);
}


void
Settings::RemoveRecent(const char* path)
{
	BAutolock lock(fLock);
	BMessage recent;
	BString existing;
	for (int32 i = 0; fData.FindString("recent", i, &existing) == B_OK; i++) {
		if (existing != path)
			recent.AddString("path", existing);
	}
	fData.RemoveName("recent");
	for (int32 i = 0; recent.FindString("path", i, &existing) == B_OK; i++)
		fData.AddString("recent", existing);
}


void
Settings::AppendRecent(const char* path)
{
	BAutolock lock(fLock);
	BString existing;
	int32 count = 0;
	for (int32 i = 0; fData.FindString("recent", i, &existing) == B_OK; i++) {
		if (existing == path)
			return;
		count++;
	}
	if (count < kMaxRecent)
		fData.AddString("recent", path);
}


bool
Settings::TrustsLinks(const char* stackId)
{
	BAutolock lock(fLock);
	return fData.GetBool(Key("links", stackId).String(), false);
}


void
Settings::SetTrustsLinks(const char* stackId)
{
	BAutolock lock(fLock);
	fData.SetBool(Key("links", stackId).String(), true);
}


bool
Settings::FetchAsked(const char* stackId)
{
	BAutolock lock(fLock);
	return fData.GetBool(Key("fetch_asked", stackId).String(), false);
}


void
Settings::SetFetchAsked(const char* stackId)
{
	BAutolock lock(fLock);
	fData.SetBool(Key("fetch_asked", stackId).String(), true);
}


BString
Settings::LastPaper()
{
	BAutolock lock(fLock);
	return fData.GetString("last_paper", "");
}


BString
Settings::LastOrientation()
{
	BAutolock lock(fLock);
	return fData.GetString("last_orientation", "portrait");
}


void
Settings::SetLastPaper(const char* size, const char* orientation)
{
	BAutolock lock(fLock);
	fData.SetString("last_paper", size);
	fData.SetString("last_orientation", orientation);
}


BString
Settings::Browser()
{
	BAutolock lock(fLock);
	return fData.GetString("browser", "");
}


void
Settings::SetBrowser(const char* path)
{
	BAutolock lock(fLock);
	fData.SetString("browser", path != NULL ? path : "");
}
