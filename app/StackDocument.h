/*
 * BDB card database - RenkuOS application
 * One open stack, shared by its stack window and list window (UI spec 2.2).
 * Every access to the core Stack happens with the document locked.
 */
#ifndef BDB_STACK_DOCUMENT_H
#define BDB_STACK_DOCUMENT_H

#include <Locker.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <String.h>

#include <memory>

#include "Stack.h"


class StackDocument {
public:
								StackDocument(std::unique_ptr<bdb::Stack> stack,
									const char* manifestPath);
								~StackDocument();

			bool				Lock() { return fLock.Lock(); }
			void				Unlock() { fLock.Unlock(); }
			bool				IsLocked() const { return fLock.IsLocked(); }

			bdb::Stack&			GetStack() { return *fStack; }
	const	BString&			Path() const { return fPath; }
			BString				RootPath() const;
			BString				Id() const { return fId; }

	// Observers receive kMsgDoc* notifications.
			void				AddObserver(BMessenger messenger);
			void				RemoveObserver(BMessenger messenger);
			int32				CountObservers() const;
			void				Notify(BMessage* message);

	// Moves a file to the Trash of its volume, remembering the original
	// path so that Tracker can restore it (UI spec 5.2).
	static	bdb::Status			MoveToTrash(const std::string& absPath);

private:
			BLocker				fLock;
			std::unique_ptr<bdb::Stack> fStack;
			BString				fPath;
			BString				fId;
			BObjectList<BMessenger, true> fObservers;
};


// RAII lock for a document.
class DocLocker {
public:
	explicit					DocLocker(StackDocument* document)
									: fDocument(document)
									{ fDocument->Lock(); }
								~DocLocker() { fDocument->Unlock(); }
private:
			StackDocument*		fDocument;
};


#endif	// BDB_STACK_DOCUMENT_H
