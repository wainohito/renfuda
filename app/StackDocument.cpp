/*
 * BDB card database - RenkuOS application
 */
#include "StackDocument.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Node.h>
#include <Path.h>
#include <Volume.h>

#include <sys/stat.h>


StackDocument::StackDocument(std::unique_ptr<bdb::Stack> stack,
	const char* manifestPath)
	:
	fLock("bdb document"),
	fStack(std::move(stack)),
	fPath(manifestPath),
	fId(fStack->Id().c_str()),
	fObservers(4)
{
	if (bdb::FolderStore* store
			= dynamic_cast<bdb::FolderStore*>(&fStack->Store())) {
		store->trashFunction = &StackDocument::MoveToTrash;
	}
}


StackDocument::~StackDocument()
{
}


BString
StackDocument::RootPath() const
{
	return BString(fStack->Store().RootPath().c_str());
}


void
StackDocument::AddObserver(BMessenger messenger)
{
	DocLocker lock(this);
	fObservers.AddItem(new BMessenger(messenger));
}


void
StackDocument::RemoveObserver(BMessenger messenger)
{
	DocLocker lock(this);
	for (int32 i = fObservers.CountItems() - 1; i >= 0; i--) {
		if (*fObservers.ItemAt(i) == messenger)
			delete fObservers.RemoveItemAt(i);
	}
}


int32
StackDocument::CountObservers() const
{
	return fObservers.CountItems();
}


void
StackDocument::Notify(BMessage* message)
{
	DocLocker lock(this);
	for (int32 i = 0; i < fObservers.CountItems(); i++)
		fObservers.ItemAt(i)->SendMessage(message, (BHandler*)NULL, 1000000);
}


bdb::Status
StackDocument::MoveToTrash(const std::string& absPath)
{
	struct stat st;
	if (lstat(absPath.c_str(), &st) != 0)
		return bdb::Status(bdb::ErrorKind::kNotFound, absPath);

	BVolume volume(st.st_dev);
	BPath trashPath;
	if (find_directory(B_TRASH_DIRECTORY, &trashPath, true, &volume) != B_OK)
		return bdb::Status(bdb::ErrorKind::kWriteFailed, "trash");
	BDirectory trash(trashPath.Path());
	if (trash.InitCheck() != B_OK)
		return bdb::Status(bdb::ErrorKind::kWriteFailed, "trash");

	BEntry entry(absPath.c_str());
	char name[B_FILE_NAME_LENGTH];
	entry.GetName(name);
	BString target(name);
	for (int32 i = 1; trash.Contains(target.String()); i++) {
		target = name;
		target << " " << i;
	}

	// Tracker uses this attribute for "Restore".
	BNode node(&entry);
	BString original(absPath.c_str());
	node.WriteAttrString("_trk/original_path", &original);
	node.Unset();

	if (entry.MoveTo(&trash, target.String()) != B_OK)
		return bdb::Status(bdb::ErrorKind::kWriteFailed, "trash");
	return bdb::Status::Ok();
}
