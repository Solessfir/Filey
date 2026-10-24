#pragma once

#include "Jobs.h"

typedef enum P_FileKind {
    P_FileKind_Directory,
    P_FileKind_File,
    P_FileKind_Other,
    P_FileKind_Drive,  // a drive's root or mount point, for its icon; listings never return it
    P_FileKind_Count,
} P_FileKind;

typedef struct P_File {
    String name;
    u64 size;
    i64 modifiedTimeUnix; // Seconds since the Unix epoch.
    i64 createdTimeUnix;  // 0 when the file system does not say
    i64 accessedTimeUnix;
    u32 modifiedTicks;  // the times' parts within their second, in 100 ns ticks, 0 where the file system does not say
    u32 createdTicks;
    u32 accessedTicks;
    enum8(P_FileKind) kind;
    b8 hidden;          // marked hidden, or named with a dot first
    b8 system;          // marked as the system's own, on Windows
    b8 metadataValid;
    b8 symbolicLink;
    b8 online;          // its contents are only in the cloud, as a synced drive's placeholders are, and reading them downloads them
    u16 mode;           // the permission bits where the system has them, as on Linux (owner, group, others: read, write, run); 0 elsewhere
} P_File;

typedef struct P_FileArray {
    ArrayHeader header;
    P_File *v;
} P_FileArray;

typedef struct P_DirectoryResult {
    P_FileArray files;
    u32 errorCode;
    String errorString;
    b32 cancelled;
    b32 hidden;  // the folder itself, as its parent's listing marks it: hidden, or named with a dot first; never a drive's root
    b32 locked;  // an archive's: some of its entries are encrypted
} P_DirectoryResult;

typedef struct P_FileError {
    u32 code;
    String message;
} P_FileError;

// Whether `error` is an operation the user stopped, as in the system's own progress window, which is no failure to report.
function b32 P_FileErrorIsCancel(P_FileError error);

// Results belong to arena. Cancellation and directory errors discard partial listings.
function P_DirectoryResult P_EnumerateDirectory(Arena *arena, String path, const P_CancelToken *cancel);
// Up to `limit` items inside an archive (zip, 7z, rar, tar and the like), each named by its path inside it with '/' between folders, folders
// as P_FileKind_Directory; false, with none, when the system cannot read it as an archive or finds nothing in it. Slow; run it in a job.
function b32 P_ListArchive(Arena *arena, String path, u64 limit, const P_CancelToken *cancel, P_FileArray *files);
// Archives open as folders. One level of the folder `inner`, empty for the top, in the archive file `archive`, as P_EnumerateDirectory gives
// a folder's: each item by its own name, the folders it is in left out.
function P_DirectoryResult P_EnumerateArchive(Arena *arena, String archive, String inner, const P_CancelToken *cancel);
typedef struct P_Progress P_Progress;
// Copies the items `names` of the folder `inner` in `archive` into the folder `directory` on disk, folders with all they hold. A name taken
// there gets a new one, as copies do. `progress`, which may be NULL, counts the archive read through and pauses as transfers do.
function P_FileError P_ExtractArchive(Arena *arena, String archive, String inner, const String *names, u64 count, String directory, P_Progress *progress,
                                      const P_CancelToken *cancel);
// Copies the items at `paths` on disk into the folder `inner` of `archive`, in place of what has their names there, making the archive when
// it is not there yet. The archive is rewritten
// beside it and put in place once complete, so that a cancel leaves it as it was; formats libarchive cannot write, such as RAR, fail.
// `level` is how hard the items are compressed, from 0, stored as they are, to 9, the smallest, where the format has levels.
function P_FileError P_AddToArchive(Arena *arena, String archive, String inner, const String *paths, u64 count, u32 level, P_Progress *progress, const P_CancelToken *cancel);
// Whether P_AddToArchive writes the archive `archive`, by its name alone: zip, 7z and tar, not RAR or cabinets, which no free library writes.
function b32 P_ArchiveWritable(String archive);
// Encrypted archives: the password every read of `archive` takes from now on in this run, kept only in memory; whether one was given; and
// whether `password` opens its encrypted entries, which reads the first one's data, slowly, so in a job. Only a zip's entries can be
// decrypted; an extraction that stops for want of the password fails with a message that says so.
function void P_ArchiveSetPassword(String archive, String password);
function b32 P_ArchiveHasPassword(String archive);
function b32 P_ArchiveCheckPassword(String archive, String password, const P_CancelToken *cancel);
// Starts dragging the items `names` of the folder `inner` in `archive`, which a drop copies out. Linux takes them out into P_RunFolder first;
// Windows drags them from the shell's archive folders.
function b32 P_DragArchiveItems(String archive, String inner, const String *names, u64 count);
// Whether `path` is a folder on disk.
function b32 P_IsDirectory(String path);
// A folder of this run's own under the system's temporary folder, for files taken out of archives, created when missing; empty when it cannot be.
function String P_RunFolder(Arena *arena);
// Removes the run folders of the runs that are over, beside this one's, with what was taken out of archives into them; one whose process
// still runs, or may, as one of another user's, stays. P__RemoveStaleRunFolders does it in the folder `root` that holds them, but for `own`.
function void P_RemoveStaleRunFolders(void);
function void P__RemoveStaleRunFolders(String root, u32 own);

typedef struct P_DirectoryTotals {
    u64 folders;
    u64 files;
    u64 bytes;
    b32 complete;  // false when cancelled or when `path` itself could not be read
} P_DirectoryTotals;

// Adds the items directly inside `path`, hidden ones included, to `totals`: its folders, its files and their bytes.
// Appends to `subfolders`, in `arena`, the paths of the folders a walk goes on into, which leaves out links and junctions,
// and on Linux the folders of other file systems, as du -x does.
// False when `path` cannot be read. Touches the disk, so run it in a job.
function b32 P_CountFolder(Arena *arena, String path, P_DirectoryTotals *totals, ArrayStr *subfolders);
// Whether `path` is a folder the system makes up rather than stores, such as Linux's /proc, which a walk of the whole drive leaves out.
function b32 P_IsVirtualFolder(String path);
// What P_LoadFileIcon takes for the icon of the computer, which lists the drives; empty where there is none to load.
function String P_ComputerIconPath(void);
// When `path` was created, in seconds since the Unix epoch; 0 when the file system does not say.
function i64 P_FileCreatedTime(String path);
// When the item at `path` last changed, in seconds since the Unix epoch; 0 when it is not there.
function i64 P_FileModifiedTime(String path);
// Lexically normalized absolute path with '/' separators; empty means failure.
function String P_AbsolutePath(Arena *arena, String path);
// The paths the app was started with as arguments, made absolute.
function ArrayStr P_StartupPaths(Arena *arena);
// `text` with the environment variables it names replaced by their values, as the system's own shell writes them: "%TEMP%" on Windows,
// "$HOME", "${HOME}" and a leading "~" on Linux, which takes "%HOME%" too. A Linux name at the very end, which may still be being typed, is
// left as it is unless `final`. Names without a value stay as they are.
function String P_ExpandVariables(Arena *arena, String text, b32 final);
// Adds to the program search path what the user's saved one has and it lacks, as a run started by the shell's own launchers, such as Win+E,
// can get an older or shortened one, which would leave the tools Filey runs, such as ffmpeg, unfound. Nothing on Linux.
function void P_CompleteSearchPath(void);
// Whether the command line has `flag`, such as "--new-window", which P_StartupPaths leaves out.
function b32 P_StartupFlag(String flag);
// What follows `prefix`, such as "--tab=", in the argument that starts with it; empty without one.
function String P_StartupOption(Arena *arena, String prefix);
function String P_FormatFileTime(Arena *arena, i64 modifiedTimeUnix);
// How far local time is ahead of UTC at `timeUnix`, in seconds, daylight saving time included.
function i64 P_LocalOffset(i64 timeUnix);
// The local date of `timeUnix` as "2026-10-16", which a file name can hold; empty when it is out of range.
function String P_FormatFileDate(Arena *arena, i64 timeUnix);
// Now, in seconds since the Unix epoch.
function i64 P_NowUnix(void);
// The order a folder is shown in, so that an app opened on one of its files can step through the rest in the same order, as from Explorer.
typedef enum P_FileOrderKey {
    P_FileOrderKey_Name,
    P_FileOrderKey_Type,
    P_FileOrderKey_Modified,
    P_FileOrderKey_Size,
    P_FileOrderKey_Count,
} P_FileOrderKey;

typedef struct P_FileOrder {
    enum8(P_FileOrderKey) key;
    b8 descending;
} P_FileOrder;

// Opens `path` with its default application.
// Windows hands packaged apps such as Photos the rest of the folder in `order`.
// May block while the application starts.
// Run in a job.
function P_FileError P_OpenPath(Arena *arena, String path, P_FileOrder order);
// Opens the web address `url` in the default browser.
function P_FileError P_OpenUrl(Arena *arena, String url);
// From now on a crash leaves a report in `folder`, named after Filey's `version` and the time: a minidump on Windows (the threads' stacks and
// what they point at), the stack's addresses and functions as text on Linux. Main thread, once, early.
function void P_CatchCrashes(String folder, String version);
// Whether a program set to open files like `path` does, other than a file manager: the system's own, as Explorer's view of a zip is, or
// Filey. An archive without one opens as a folder.
function b32 P_OpensInApp(String path);
// Opens the system's own file manager on the folder `path`: Explorer on Windows, and on Linux the application the desktop opens folders with,
// Filey's own entry left out.
function P_FileError P_OpenFileExplorer(Arena *arena, String path);
// Starts another Filey window, a process of its own, showing the folder `path`, with `option`, when not empty, as one more argument.
function P_FileError P_OpenWindow(Arena *arena, String path, String option);
// The running program's file.
function String P_ExecutablePath(Arena *arena);
// Puts the file `replacement` where the running program's file `executable` is, in one step: on Windows the running one moves aside first,
// to "<name>.old.exe" beside it, or "<name>.old-<process>.exe" while an older one still runs from that; elsewhere the new one is renamed
// over it, which leaves the running program its old file, and copied beside it first when it is on another file system.
function P_FileError P_ReplaceExecutable(Arena *arena, String executable, String replacement);
// Removes what replacing `executable` moved aside, each copy once nothing runs it; there is nothing to remove but on Windows.
function void P_RemoveReplaced(String executable);
// Starts `executable` with --after=<this process>, which waits for this process to end before it takes the window over.
function P_FileError P_Restart(Arena *arena, String executable);
// Waits until process `process` has ended, or `timeoutMilliseconds` have passed.
function void P_WaitForExit(u32 process, u32 timeoutMilliseconds);
// Starts the system's console in the folder `directory`: cmd.exe in the default terminal on Windows, the desktop's terminal on Linux.
function P_FileError P_OpenConsole(Arena *arena, String directory);
// Reports changes to the items of one folder, not of its subfolders, from a thread of its own, at most every P_WatchPause milliseconds, so
// that a folder that keeps changing, as one a download writes into, or a burst such as a copy or a build, brings a few reloads.
// {0} is a stopped watch.
// `wake` runs on that thread when a change follows a call to P_DirectoryWatchTakeChanged.
#define P_WatchPause 100
typedef struct P_DirectoryWatch {
    Arena arena;
    void *native;
} P_DirectoryWatch;

function b32 P_DirectoryWatchStart(P_DirectoryWatch *watch, String path, P_JobWakeCallback *wake, void *user);
// The same for everything below `path` on Windows, which watches a whole tree with one handle; Linux's inotify watches one folder each, so
// there it is P_DirectoryWatchStart.
function b32 P_DirectoryWatchStartTree(P_DirectoryWatch *watch, String path, P_JobWakeCallback *wake, void *user);
// True once for any number of changes since the previous call.
function b32 P_DirectoryWatchTakeChanged(P_DirectoryWatch *watch);
function void P_DirectoryWatchStop(P_DirectoryWatch *watch);

// Renames `from` to `to` inside `directory`.
// Never replaces an existing item, and refuses names that are empty, "." or "..", or contain a path separator.
// May touch a slow disk, so run it in a job.
function P_FileError P_RenamePath(Arena *arena, String directory, String from, String to);
// Gives the item at `path` the permission bits `mode`, as P_File's are; where the system has none, as on Windows, it fails.
function P_FileError P_SetMode(Arena *arena, String path, u16 mode);

// File operations Filey carries out itself.
// Each may touch slow disks or many files, so run them in jobs.
//
// Creates an empty folder or file called `name` inside `directory`, numbering the name when it is taken ("New folder (2)"), and returns the name used in `created`.
function P_FileError P_CreateItem(Arena *arena, String directory, String name, b32 folder, String *created);
// Makes a shortcut to `target` inside `directory` (a .lnk on Windows, a symbolic link elsewhere), numbering its name when it is taken, and returns
// the name used in `created`.
function P_FileError P_CreateLink(Arena *arena, String directory, String target, String *created);
// What a copy or move does with a name already taken in the target folder.
typedef enum P_Conflict {
    P_Conflict_Ask,       // waits for the program's answer through P_Progress
    P_Conflict_KeepBoth,  // numbers the new item's name
    P_Conflict_Replace,   // the new file takes the name once it is complete; a folder is never replaced, so both are kept
    P_Conflict_Skip,
    P_Conflict_Count,
} P_Conflict;

// A long file operation's progress, which its job writes and the main thread reads, and the questions the main thread answers.
// While `asking` is set the job waits on the main thread, which stores a P_Conflict in `answer` (and in `policy` too to answer for the rest) and then clears `asking`.
typedef struct P_Progress {
    atomic_uint_fast64_t totalBytes, doneBytes, totalItems, doneItems;
    atomic_bool asking;
    atomic_bool paused;       // set by the program, which makes the job wait before its next step until it is cleared or the operation cancelled
    atomic_uint answer;
    atomic_uint policy;       // a P_Conflict, set before the operation starts
    b32 conflictFolder;       // what is asked about: the existing item `conflictName` in the target folder
    char conflictName[256];
    // The name of the file being copied, which the job rewrites as it goes; `nameVersion` is odd while it does, so that a read across a rewrite is told.
    atomic_uint nameVersion;
    atomic_char name[256];
} P_Progress;

// Puts `name` up as the item being worked on, for the program to show, cut to what `progress` holds.
function void P_ProgressName(P_Progress *progress, String name);

// Copies or moves the items at `paths` into `directory`.
// `progress`, when not NULL, is kept current and says what taken names do; without it a taken name gets a number.
// `placed`, when not NULL, receives in `arena` the path each item ended up at, in the order of `paths`, empty for one that was skipped.
// Windows asks with its own conflict and progress windows instead, and the shell's "- Copy" names replace numbers.
// A folder is never put inside itself, and moving an item into the folder it is in does nothing.
// Linux moves across file systems by copying and removes the source only once its copy is complete. It writes each file unnamed and names it
// once complete, or where the file system cannot, as FAT and NTFS, under a hidden name, ".<name>.part", so that a copy cut off, as by a drive
// pulled out, never leaves a shortened file under the real name.
function P_FileError P_TransferItems(Arena *arena, const String *paths, u64 count, String directory, b32 move, P_Progress *progress, ArrayStr *placed, const P_CancelToken *cancel);
// Moves the items at `paths` to the Trash, or the Recycle Bin on Windows, which asks before deleting anything it cannot keep.
function P_FileError P_TrashItems(Arena *arena, const String *paths, u64 count);

// An item in the Recycle Bin or the Trash: its file where the bin keeps it, `file.name` that whole path, with its own name, where it came from
// and when it was deleted.
typedef struct P_Trashed {
    P_File file;
    String name;
    String original;  // the folder it was in
    i64 deletedTimeUnix;
} P_Trashed;

typedef struct P_ArrayTrashed { ArrayHeader header; P_Trashed *v; } P_ArrayTrashed;

// The kind of drive a folder is on, for work that is cheap on some and slow on others; a local drive that cannot be told apart counts as a solid-state one.
typedef enum P_DriveKind {
    P_DriveKind_SolidState,
    P_DriveKind_Spinning,
    P_DriveKind_Network,
    P_DriveKind_Count,
} P_DriveKind;

// The kind of drive `path` is on; it asks the system each time, so it belongs in background jobs.
function P_DriveKind P_PathDriveKind(String path);

// Whether the system asks apps for dark colors: Windows' app mode, or on Linux the GTK theme; dark without a word either way.
function b32 P_PrefersDarkColors(void);

// The folder the user's deleted items are listed in: the system drive's $Recycle.Bin, or the home Trash; P_UserPlaces lists it too.
function String P_TrashFolder(Arena *arena);
// True when `path` is a bin P_ListTrash reads: a drive's $Recycle.Bin, or the home Trash.
function b32 P_IsTrashFolder(String path);
// Whether the Recycle Bin, the system drive's on Windows, or the Trash holds anything; it reads the bin, so it is asked on a click, not every frame.
function b32 P_TrashHasItems(void);
// Deletes everything in the Recycle Bin of every drive, or in the Trash, for good. With `confirm`, Windows asks first with its own
// confirmation, which P_EmptyTrashAsks says; elsewhere it never asks, and the program confirms first.
function P_FileError P_EmptyTrash(Arena *arena, b32 confirm);
function b32 P_EmptyTrashAsks(void);
// What the bin at `path` holds; on Windows, the items of the user's own folders in it.
function P_ArrayTrashed P_ListTrash(Arena *arena, String path, const P_CancelToken *cancel);
// Puts the items of the bin `bin` at `paths` (P_ListTrash's file names), or all of them when `count` is 0, back where they came from, making
// the folders that have gone since. One whose place something else has taken stays in the bin, which the error then tells of.
function P_FileError P_RestoreTrash(Arena *arena, String bin, const String *paths, u64 count);
// Deletes the items at `paths` for good, folders with everything in them; links are removed, never followed. With `confirm`, Windows
// first asks with its own confirmation, which P_DeleteAsks says, and a No is not an error; elsewhere it never asks, and the program
// confirms first.
function P_FileError P_DeleteItems(Arena *arena, const String *paths, u64 count, b32 confirm, const P_CancelToken *cancel);
function b32 P_DeleteAsks(void);
// Whether files can be marked as the system's own, as P_File.system says on Windows, which showing or hiding them needs.
function b32 P_HasSystemFiles(void);
// A whole volume's folders and files read from its file system's own table rather than folder by folder, in breadth-first order from
// its root: each entry's name, the entry of the folder it is in (UINT32_MAX for the root's own), and whether it is a folder, hidden or a
// link, which is not gone into.
typedef struct P_VolumeEntry {
    String name;
    u32 parent;
    b8 folder;
    b8 hidden;
    b8 link;
} P_VolumeEntry;
typedef struct P_VolumeTree {
    P_VolumeEntry *entries;
    u64 count;
} P_VolumeTree;
// Reads it for the drive whose root is `root`, as "C:/", at most `most` entries; false where that cannot be done, as on Windows without
// an administrator's rights or on another file system than NTFS, and always on Linux.
function b32 P_ReadVolumeTree(Arena *arena, String root, u64 most, const P_CancelToken *cancel, P_VolumeTree *tree);
// Whether the system limits the programs that do not ask past it to paths of 260 characters, as Windows does unless its LongPathsEnabled
// is set; Filey's own paths go past it either way.
function b32 P_HasPathLimit(void);
// Whether programs may use paths longer than 260 characters: Windows' LongPathsEnabled.
function b32 P_LongPathsEnabled(void);
// Sets LongPathsEnabled, which asks for an administrator's consent, and waits until that is done, so a job calls it; false when it was
// not set, as when the consent was refused.
function b32 P_SetLongPaths(b32 on);
// What the system calls its bin: "Recycle Bin" on Windows, "Trash" on Linux.
function String P_TrashName(void);
// The two-letter code of the language the user's system shows its text in, as "ru"; empty when it says none.
function String P_SystemLanguage(Arena *arena);
// `path` with the system's own separators, for showing or copying.
function String P_NativePath(Arena *arena, String path);

// Sidebar locations.
typedef struct P_Place {
    String name;
    String path;
    u64 totalBytes;  // drives only; 0 when the drive has no medium or does not say
    u64 freeBytes;
    String type;        // drives only: what kind of drive it is, as "Local Disk"
    String fileSystem;  // drives only: as "NTFS" or "ext4"; empty without a medium
    String id;          // drives only: the volume's UUID on Linux, as "FEBA1237BA11ED41", which Go To finds it by too; empty where none is known
    String remote;      // network drives only: the share it stands for, as "//nas/media"; empty where the system does not say
    b8 busy;            // drives only: did not answer in time, such as one spinning up a disc, so its details come with a later listing
    b8 home;            // the user's home, which goes by its folder's name, the account's
} P_Place;

typedef struct P_ArrayPlace { ArrayHeader header; P_Place *v; } P_ArrayPlace;

// The user's home and its desktop, documents, downloads, music, pictures and videos folders that exist.
function P_ArrayPlace P_UserPlaces(Arena *arena);
// Folders that the breadcrumbs of the folders inside them start from: the user's places, and on Windows the local and roaming app data.
function ArrayStr P_PathRoots(Arena *arena);
// Folders Go To searches besides the user's places that exist: on Windows the app data folders, the Start menu and Startup, Favorites,
// Program Files, the users' folder, the system's, the fonts', ProgramData and Windows; elsewhere the configuration, data and cache folders,
// the applications' and fonts' folders, /etc and /opt.
function ArrayStr P_SearchPlaces(Arena *arena);
// The folders server `server`, as "//nas", shares, each a place named "media (\\nas)" with its size where the server says; none where it
// shares none or does not answer, or where the system has no way to ask (Linux, where mounted shares are drives). A server that does not
// answer takes a while to give up, so run it in a job.
function P_ArrayPlace P_ServerShares(Arena *arena, String server, const P_CancelToken *cancel);
// Mounted drives with their sizes; network drives may be slow to answer, so run it in a job.
function P_ArrayPlace P_Drives(Arena *arena);
// The folder Filey keeps its settings in, created when missing; empty when there is none.
// FILEY_CONFIG_DIR, when set, names the folder instead, for portable or test setups.
function String P_ConfigDirectory(Arena *arena);
// The folder for what is kept for this machine only, such as the drive index; the settings folder when FILEY_CONFIG_DIR is set.
function String P_CacheDirectory(Arena *arena);
// The whole file; false when it cannot be read.
function b32 P_ReadFile(Arena *arena, String path, String *contents);
// Reads at most `limit` bytes from the start of the file at `path`.
function b32 P_ReadFileStart(Arena *arena, String path, u64 limit, String *contents);
// Runs `arguments[0]`, found on the PATH, with the rest of `arguments` in `directory`, without a window, and gives what it wrote to its standard
// output, with `errors` what it wrote to its standard error too, such as git's messages; false when it could not start, ran past
// `timeoutMilliseconds` (it is then stopped, with what it started) or exited with an error.
// Helpers it starts may outlive it once it ended, as git's file system monitor does.
function b32 P_RunCommand(Arena *arena, String directory, const String *arguments, u32 count, u32 timeoutMilliseconds, String *output, b32 errors);
// Whether the program `name` is on the PATH, as P_RunCommand would find it.
function b32 P_ProgramFound(String name);
// P_RunCommand with `environment`, "NAME=value" each, set for the command alone over the program's own.
// `cancel`, which may be NULL, stops it with what it started.
function b32 P_RunCommandWith(Arena *arena, String directory, const String *arguments, u32 count, const String *environment, u32 environmentCount,
                              u32 timeoutMilliseconds, String *output, b32 errors, const P_CancelToken *cancel);
// The program's environment variable `name`; empty when it is not set.
function String P_EnvironmentValue(Arena *arena, String name);
// What a command writes, handed over as it comes.
typedef void P_CommandOutput(void *user, String text);
// Runs a command as P_RunCommand does but without a time limit, handing what it writes to `output` as it comes. `cancel` stops it and the
// programs it started, and `progress`, which may be NULL, suspends them while it says paused. False when it could not start, was stopped
// or exited with an error.
function b32 P_RunCommandStream(String directory, const String *arguments, u32 count, b32 errors, P_CommandOutput *output, void *user, const P_CancelToken *cancel,
                                P_Progress *progress);
// Sound out, 16-bit samples interleaved over `channels` at `rate` a second, through the system's mixer, with about a tenth of a second
// queued ahead. NULL where there is no sound.
typedef struct P_Audio P_Audio;
function P_Audio *P_AudioOpen(Arena *arena, u32 rate, u32 channels);
// Queues `frames` frames, waiting while the queue is full; gives the frames heard so far.
function u64 P_AudioWrite(P_Audio *audio, const i16 *samples, u32 frames);
// Stops it, at once or with `drain` once what is queued has been heard.
function void P_AudioClose(P_Audio *audio, b32 drain);
// Whether anything is at `path`, a link itself included.
function b32 P_PathExists(String path);
// Where the item at `path` really is: a shortcut's or link's target, with the links on the way to it followed; false when that cannot be told,
// as for a link whose target is gone.
function b32 P_RealPath(Arena *arena, String path, String *real);
// Waits `milliseconds`, for a job that waits on something outside the program.
function void P_Sleep(u32 milliseconds);
// Waits up to `milliseconds` until nothing is left at any of `paths`, as after the shell started moving them; false when something still is.
function b32 P_WaitGone(const String *paths, u64 count, u32 milliseconds);
// The size in bytes of the file at `path`; false when there is none.
function b32 P_FileSize(String path, u64 *size);
// Up to `limit` bytes of `path` from `offset` on; fewer past its end.
function b32 P_ReadFileAt(Arena *arena, String path, u64 offset, u64 limit, String *contents);
// Replaces the file with `contents` in one step, so that a crash leaves the old or the new file and never half of one.
function P_FileError P_WriteFile(Arena *arena, String path, String contents);
// Adds `contents` to the end of the file at `path`, which `restart` empties first; the file is made when missing.
function P_FileError P_AppendFile(Arena *arena, String path, String contents, b32 restart);
// Moves the file at `from` over the one at `to` in one step.
function P_FileError P_ReplaceFile(Arena *arena, String from, String to);

// Straight-alpha RGBA8 pixels (red in the lowest byte), top row first.
typedef struct P_Image {
    u32 *pixels;
    i32 width;
    i32 height;
} P_Image;

// Installing Filey for this user, which only Windows has: the running executable copied into a folder of its own under the local
// application data, a Start menu shortcut to that copy, an entry in the system's list of installed apps, which uninstalls it, and on request
// a desktop shortcut, the folder on the user's PATH, the copy as what opens folders in place of Explorer, and entries in Explorer's
// right-click menus. Nothing needs elevation, and only what the install made is taken away again.
typedef struct P_Install {
    String folder;         // where the copy goes
    String version;        // the installed copy's, when there is one
    b32 installed;         // a copy is there
    b32 running;           // the running executable is that copy
    b32 same;              // the copy has the running executable's bytes
    b32 desktopShortcut;   // a desktop shortcut to the copy is there
    b32 onPath;            // the folder is on the user's PATH
    b32 setDefault;        // folders, drives and Win+E open the copy, in place of Explorer
    b32 contextMenu;       // Explorer's right-click menus show a file in it, open a folder in it, and open it in a folder's background
} P_Install;

// The right-click entries' words, in the language shown: a file's, a folder's and a folder's background's.
typedef struct P_MenuLabels {
    String file, folder, background;
} P_MenuLabels;

function b32 P_CanInstall(void);
// Reads the files and the registry.
function P_Install P_InstallState(Arena *arena);
// Installs, replacing a copy that is there, or changes the shortcuts, the PATH and the default of the install; the running process goes on running
// from where it started, and the shortcuts start the copy.
function P_FileError P_InstallApply(Arena *arena, b32 desktopShortcut, b32 onPath, b32 setDefault, b32 contextMenu, P_MenuLabels labels);
function P_FileError P_Uninstall(Arena *arena);
// The application's own icon at `size` pixels.
function b32 P_LoadAppIcon(Arena *arena, i32 size, P_Image *image);

// Key shared by files of one type: lowercase ".ext", or the folder and extensionless-file keys.
// Pure string work, safe while building UI.
function String P_FileTypeKey(Arena *arena, String path, P_FileKind kind);
// The type key, except that files with their own icon (executables, shortcuts) are keyed by path.
function String P_FileIconKey(Arena *arena, String path, P_FileKind kind);
// Whether the system's icon theme changed since this was last asked, after which icons loaded before are out of date; it wakes the window.
function b32 P_IconThemeChanged(void);
// The system icon for `path`, at least `size` pixels when available, which the display's `scale` gave;
// Linux icon themes pick their artwork by the logical size and the whole scale.
// The image may be larger or smaller; scaling is left to the caller.
// May touch the disk, so run it in a job.
// `exact` asks for the icon of this very item, as for special folders such as the desktop, rather than the one of its type.
// A bare name, with no folder, gets its type's icon, from the name alone.
function b32 P_LoadFileIcon(Arena *arena, String path, P_FileKind kind, i32 size, f32 scale, b32 exact, P_Image *image);
// A large picture for icon views, fitting a size x size square: the system's thumbnail where it makes one (an image's contents, a folder's preview),
// else the icon unless `thumbnailOnly`, which then fails. A program Linux runs to make it is stopped once `cancel`, which may be NULL, is.
function b32 P_LoadFileThumbnail(Arena *arena, String path, P_FileKind kind, i32 size, b32 thumbnailOnly, const P_CancelToken *cancel, P_Image *image);
// The thumbnail the system already keeps for `path`, without making one; false when it has none, which on Linux is always.
function b32 P_LoadCachedThumbnail(Arena *arena, String path, P_FileKind kind, i32 size, P_Image *image);
// The picture in an image file at its own size, or scaled down to fit a `maxSide` square when larger.
// `sourceWidth` and `sourceHeight` get the size it has. False for files that are not pictures the system can read.
// Reads the whole file, so run it in a job.
function b32 P_LoadImageFile(Arena *arena, String path, i32 maxSide, P_Image *image, i32 *sourceWidth, i32 *sourceHeight);
// The same for a picture file's bytes already in memory, as one a 3D model embeds.
function b32 P_LoadImageBytes(Arena *arena, String data, i32 maxSide, P_Image *image);
// The pages of the PDF at `path`, at most `most`, with each one's width and height in `sizes`, in the arena; 0 when it cannot be read,
// which on Linux is always.
function u32 P_PdfPages(Arena *arena, String path, u32 most, f32 **sizes);
// Page `page` of the PDF at `path` drawn `width` pixels wide.
function b32 P_LoadPdfPage(Arena *arena, String path, u32 page, i32 width, P_Image *image);
// Thumbnails are keyed by path where the system makes them per file, and by type where it does not.
function String P_FileThumbnailKey(Arena *arena, String path, P_FileKind kind);
// Display name of a file's type ("Text Document", "File folder") from the shell on Windows and shared-mime-info on Linux.
// Only the name is used, never the file.
// Slow enough to run in a job and cache per P_FileTypeKey.
function String P_FileTypeName(Arena *arena, String name, P_FileKind kind);

// Context menus.
// Windows reads the shell's own menu, extensions included; Linux offers the actions Filey implements itself.
// Those items carry an action, which the program carries out instead of P_ContextMenuInvoke.
typedef enum P_MenuAction {
    P_MenuAction_None,
    P_MenuAction_Cut,
    P_MenuAction_Copy,
    P_MenuAction_Paste,
    P_MenuAction_Duplicate,
    P_MenuAction_CopyAsPath,
    P_MenuAction_Trash,
    P_MenuAction_Delete,
    P_MenuAction_NewFolder,
    P_MenuAction_NewFile,
    P_MenuAction_EmptyTrash,
    P_MenuAction_Restore,
    P_MenuAction_RestoreAll,
    P_MenuAction_CompressZip,
    P_MenuAction_Compress7z,
    P_MenuAction_ExtractHere,
    P_MenuAction_ExtractToFolder,
    P_MenuAction_CompressVideo,
    P_MenuAction_DownloadTorrent,
    P_MenuAction_AddToGroup,  // the item's id is the group
    P_MenuAction_Count,
} P_MenuAction;

typedef struct P_MenuItem {
    String label;
    String verb;       // canonical verb such as "open" or "delete", in lowercase, when the system has one
    u32 id;            // passed back to P_ContextMenuInvoke; 0 for separators
    u32 firstChild;    // index into P_ContextMenu.items
    u32 childCount;    // nonzero for submenus
    P_Image icon;      // the item's own picture at its native size, in the menu's arena; empty when it has none
    b8 separator;
    b8 disabled;
    b8 isDefault;
    enum8(P_MenuAction) action;
} P_MenuItem;

typedef struct P_ContextMenu {
    P_MenuItem *items;  // the top level is [0, rootCount); each submenu's children are contiguous
    u32 count;
    u32 rootCount;
    void *native;       // platform state that keeps the menu alive until P_ContextMenuClose
} P_ContextMenu;

// Menu for `names` inside `directory`, or for the directory background when `count` is 0.
// With an empty `directory`, `names` are full paths that share a folder, such as drives, and none at all is the computer's own background, as
// This PC lists the drives.
// Main thread only. Windows builds, invokes and releases shell menus on a thread of their own, as they belong to the thread that built
// them and their verbs may need its message loop (clipboard, dialogs), and waits for that thread here; loading shell extensions can take
// a moment, which a menu prepared with the same arguments has taken already.
function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu);
// Builds the menu P_ContextMenuOpen would give for the same arguments ahead of time, off the main thread, once they have stayed the same
// for a moment, so that a right-click finds it ready; it is built again once taken or stale. Cheap when nothing changed. Windows only.
function void P_ContextMenuPrepare(String directory, const String *names, u64 count);
// Gets the first menu ready ahead, on any thread: Windows starts its menu thread, Linux loads the application database.
function void P_ContextMenuWarm(String directory, String name);
// A verb that opens a dialog of its own goes on with it after this returns, and its failure is not reported.
function P_FileError P_ContextMenuInvoke(Arena *arena, P_ContextMenu *menu, u32 id);
function void P_ContextMenuClose(P_ContextMenu *menu);

// Drag and drop between Filey and other programs, through the system's own drag protocol.
// Main thread only.
//
// Starts a drag of `names` inside `directory` and returns once they are dropped (true) or the drag is cancelled.
// The system's drag loop runs inside this call and renders frames through the modal frame callback while the pointer is over the window, so call it outside a frame.
function b32 P_DragFiles(String directory, const String *names, u64 count);
// Opens the system's properties window for `names` inside `directory`; false where it has none, as on Linux.
function b32 P_ShowProperties(String directory, const String *names, u64 count);
// The folder that files dropped on the window go into, updated as the pointer moves; empty refuses drops.
function void P_SetDropDirectory(String path);
// While on, drops are not transferred: the folders among the dropped items are kept for P_TakeDroppedFolders, as a place to pin them is under the pointer.
function void P_SetDropTakesFolders(b32 takes);
// The path P_SetDropDirectory gives is a file whose own handler takes the drop, as Explorer starts a program or a script with the files dropped
// on it; only Windows has such handlers.
function void P_SetDropOnFile(b32 onFile);
// The folders dropped since the last call while P_SetDropTakesFolders was on, copied into `arena`.
function ArrayStr P_TakeDroppedFolders(Arena *arena);
// A drag, Filey's own or another program's, is over the window.
function b32 P_DropPending(void);
// Where the system leaves dropped files to Filey, as on Linux and for a folder inside an archive on Windows, the files dropped into a folder
// since the last call, copied into `arena`, with that folder and whether they move, for the program to copy or move as a paste does.
// Windows' shell carries other drops out itself.
function ArrayStr P_TakeDroppedFiles(Arena *arena, String *directory, b32 *move);
// The names of the items the system's shell dropped into `directory` since the last call, as Windows carries out drops itself, copied into
// `arena`, for the program to select once they show; Linux's drops come through P_TakeDroppedFiles instead.
function ArrayStr P_TakeShellDropped(Arena *arena, String *directory);
