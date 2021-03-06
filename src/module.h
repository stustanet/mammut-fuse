#pragma once

#include "mammut_config.h"
#include "config.h"

#include <atomic>
#include <map>
#include <mutex>
#include <unordered_map>

#include <fuse.h>

#include <dirent.h>

namespace mammutfs {

class Communicator;
class MammutConfig;
/**
 * This the base fuse module, implementing a text replacement fuse translator
 *
 * Every syscall from fuse is passed into this module, where the relative fuse-path
 * will be concatenated with the raid-path + user, generating a normal filesystem path,
 * that has to exist on the host system.
 * The syscall is then forwarded to this file, relievung us of the burdon of writing a
 * full filesystem.
 */
class Module {
public:
	Module (const std::string &modname,
	        const std::shared_ptr<MammutConfig> &config,
	        const std::shared_ptr<Communicator> &comm);

	enum class LOG_LEVEL : int {
		TRACE = 0,
		INFO = 1,
		WRN = 2,
		ERR = 3,
	};

	void log(LOG_LEVEL lvl, int errnum, const std::string &msg, const std::string &path = "" );

#ifdef ENABLE_TRACELOG
	void trace(const std::string &method,
	           const std::string &path,
	           const std::string &second_path = "");
#else
	inline void trace(const std::string &,
	                  const std::string &,
	                  const std::string & = "") {};
#endif

	void info(const std::string &method,
	          const std::string &message,
	          const std::string &path,
	          const std::string &second_path = "");

	void warn(int errnum,
	          const std::string &method,
	          const std::string &message,
	          const std::string &path,
	          const std::string &second_path = "");

	void error(int errnum,
	           const std::string &method,
	           const std::string &path,
	           const std::string &second_path = "");

	/** Translate a path for normal mammut operation
	 *
	 * This is the most central function of the full system
	 * It translates the pathes like this:
	 *
	 * /module/xx/yy --> /srv/raids/raid1/user/xx/yy
	 *
	 * The raid path is determined with @find_raid, and the username comes
	 * from the config file
	 */
	virtual int translatepath(const std::string &path, std::string &out);

	/** Check if a path is valid and should be displayed / accessible
	 *
	 * This can be used to hide certain files for the user but keep them for
	 * Management purposes (for example .mammut_suffix in anon folders)
	 */
	virtual bool is_path_valid(const std::string &/*path*/) {
		return true;
	};

	/** Locates a user on a raid
	 *
	 * locates a user on a raid for a given module.
	 */
	virtual int find_raid(std::string &path);

	/**
	 * Option, if the module should be visible in the default lister
	 */
	virtual bool visible_in_root() { return true; }

	/** Get file attributes.
	 *
	 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
	 * ignored. The 'st_ino' field is ignored except if the 'use_ino'
	 * mount option is given.
	 */
	virtual int getattr(const char *, struct stat *);

	/** Read the target of a symbolic link
	 *
	 * The buffer should be filled with a null terminated string.  The
	 * buffer size argument includes the space for the terminating
	 * null character.	If the linkname is too long to fit in the
	 * buffer, it should be truncated.	The return value should be 0
	 * for success.
	 */
	virtual int readlink(const char *, char *, size_t);

	/* Deprecated, use readdir() instead */
	int getdir(const char *, fuse_dirh_t, fuse_dirfil_t);

	/** Create a file node
	 *
	 * This is called for creation of all non-directory, non-symlink
	 * nodes.  If the filesystem defines a create() method, then for
	 * regular files that will be called instead.
	 */
	virtual int mknod(const char *, mode_t, dev_t);

	/** Create a directory
	 *
	 * Note that the mode argument may not have the type specification
	 * bits set, i.e. S_ISDIR(mode) can be false.  To obtain the
	 * correct directory type bits use  mode|S_IFDIR
	 * */
	virtual int mkdir(const char *, mode_t);

	/** Remove a file */
	virtual int unlink(const char *);

	/** Remove a directory */
	virtual int rmdir(const char *);

	/** Create a symbolic link */
	virtual int symlink(const char *, const char *);

	/** Rename a file */
	virtual int rename(const char *from,
	                   const char *to,
	                   const char *from_raw,
	                   const char *to_raw);

	/** Change the permission bits of a file */
	virtual int chmod(const char *, mode_t);

	/** Change the owner and group of a file */
	virtual int chown(const char *, uid_t, gid_t);

	/** Change the size of a file */
	virtual int truncate(const char *, off_t);

	/** File open operation
	 *
	 * No creation (O_CREAT, O_EXCL) and by default also no
	 * truncation (O_TRUNC) flags will be passed to open(). If an
	 * application specifies O_TRUNC, fuse first calls truncate()
	 * and then open(). Only if 'atomic_o_trunc' has been
	 * specified and kernel version is 2.6.24 or later, O_TRUNC is
	 * passed on to open.
	 *
	 * Unless the 'default_permissions' mount option is given,
	 * open should check if the operation is permitted for the
	 * given flags. Optionally open may also return an arbitrary
	 * filehandle in the fuse_file_info structure, which will be
	 * passed to all file operations.
	 *
	 * Changed in version 2.2
	 */
	virtual int open(const char *, struct fuse_file_info *);

	/** Read data from an open file
	 *
	 * Read should return exactly the number of bytes requested except
	 * on EOF or error, otherwise the rest of the data will be
	 * substituted with zeroes.	 An exception to this is when the
	 * 'direct_io' mount option is specified, in which case the return
	 * value of the read system call will reflect the return value of
	 * this operation.
	 *
	 * Changed in version 2.2
	 */
	virtual int read(const char *, char *, size_t, off_t,
	         struct fuse_file_info *);

	/** Write data to an open file
	 *
	 * Write should return exactly the number of bytes requested
	 * except on error.	 An exception to this is when the 'direct_io'
	 * mount option is specified (see read operation).
	 *
	 * Changed in version 2.2
	 */
	virtual int write(const char *, const char *, size_t, off_t,
	          struct fuse_file_info *);

	/** Get file system statistics
	 *
	 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
	 *
	 * Replaced 'struct statfs' parameter with 'struct statvfs' in
	 * version 2.5
	 */
	virtual int statfs(const char *, struct statvfs *);

	/** Possibly flush cached data
	 *
	 * BIG NOTE: This is not equivalent to fsync().  It's not a
	 * request to sync dirty data.
	 *
	 * Flush is called on each close() of a file descriptor.  So if a
	 * filesystem wants to return write errors in close() and the file
	 * has cached dirty data, this is a good place to write back data
	 * and return any errors.  Since many applications ignore close()
	 * errors this is not always useful.
	 *
	 * NOTE: The flush() method may be called more than once for each
	 * open().	This happens if more than one file descriptor refers
	 * to an opened file due to dup(), dup2() or fork() calls.	It is
	 * not possible to determine if a flush is final, so each flush
	 * should be treated equally.  Multiple write-flush sequences are
	 * relatively rare, so this shouldn't be a problem.
	 *
	 * Filesystems shouldn't assume that flush will always be called
	 * after some writes, or that if will be called at all.
	 *
	 * Changed in version 2.2
	 */
	virtual int flush(const char *, struct fuse_file_info *);

	/** Release an open file
	 *
	 * Release is called when there are no more references to an open
	 * file: all file descriptors are closed and all memory mappings
	 * are unmapped.
	 *
	 * For every open() call there will be exactly one release() call
	 * with the same flags and file descriptor.	 It is possible to
	 * have a file opened more than once, in which case only the last
	 * release will mean, that no more reads/writes will happen on the
	 * file.  The return value of release is ignored.
	 *
	 * Changed in version 2.2
	 */
	virtual int release(const char *, struct fuse_file_info *);

	/** Synchronize file contents
	 *
	 * If the datasync parameter is non-zero, then only the user data
	 * should be flushed, not the meta data.
	 *
	 * Changed in version 2.2
	 */
	virtual int fsync(const char *, int, struct fuse_file_info *);

	/** Set extended attributes */
	virtual int setxattr(const char *, const char *, const char *, size_t, int);

	/** Get extended attributes */
	virtual int getxattr(const char *, const char *, char *, size_t);

	/** List extended attributes */
	virtual int listxattr(const char *, char *, size_t);

	/** Remove extended attributes */
	virtual int removexattr(const char *, const char *);

	/** Open directory
	 *
	 * Unless the 'default_permissions' mount option is given,
	 * this method should check if opendir is permitted for this
	 * directory. Optionally opendir may also return an arbitrary
	 * filehandle in the fuse_file_info structure, which will be
	 * passed to readdir, closedir and fsyncdir.
	 *
	 * Introduced in version 2.3
	 */
	virtual int opendir(const char *, struct fuse_file_info *);

	/** Read directory
	 *
	 * Mammutfs: Lists all files from the translated target directory
	 *
	 * This supersedes the old getdir() interface.  New applications
	 * should use this.
	 *
	 * The filesystem may choose between two modes of operation:
	 *
	 * 1) The readdir implementation ignores the offset parameter, and
	 * passes zero to the filler function's offset.  The filler
	 * function will not return '1' (unless an error happens), so the
	 * whole directory is read in a single readdir operation.  This
	 * works just like the old getdir() method.
	 *
	 * 2) The readdir implementation keeps track of the offsets of the
	 * directory entries.  It uses the offset parameter and always
	 * passes non-zero offset to the filler function.  When the buffer
	 * is full (or an error happens) the filler function will return
	 * '1'.
	 *
	 * Introduced in version 2.3
	 */
	virtual int readdir(const char *, void *, fuse_fill_dir_t, off_t,
	                    struct fuse_file_info *);

	/** Release directory
	 *
	 * Introduced in version 2.3
	 */
	virtual int releasedir(const char *, struct fuse_file_info *);

	/** Synchronize directory contents
	 *
	 * If the datasync parameter is non-zero, then only the user data
	 * should be flushed, not the meta data
	 *
	 * Introduced in version 2.3
	 */
	virtual int fsyncdir(const char *, int, struct fuse_file_info *);

	/**
	 * Check file access permissions
	 *
	 * This will be called for the access() system call.  If the
	 * 'default_permissions' mount option is given, this method is not
	 * called.
	 *
	 * This method is not called under Linux kernel versions 2.4.x
	 *
	 * Introduced in version 2.5
	 */
	virtual int access(const char *, int);

	/**
	 * Create and open a file
	 *
	 * If the file does not exist, first create it with the specified
	 * mode, and then open it.
	 *
	 * If this method is not implemented or under Linux kernel
	 * versions earlier than 2.6.15, the mknod() and open() methods
	 * will be called instead.
	 *
	 * Introduced in version 2.5
	 */
	virtual int create(const char *, mode_t, struct fuse_file_info *);

	/**
	 * Change the access and modification times of a file with
	 * nanosecond resolution
	 *
	 * This supersedes the old utime() interface.  New applications
	 * should use this.
	 *
	 * See the utimensat(2) man page for details.
	 *
	 * Introduced in version 2.6
	 */
	virtual int utimens(const char *, const struct timespec tv[2]);

protected:
	/** Reference to the global config file */
	std::shared_ptr<MammutConfig> config;

	std::shared_ptr<Communicator> comm;

	/** Name of the module to be set by child classes */
	std::string modname;

	/** The basepath that should be used to translate the path */
	std::string basepath;

	/** The currently set log level */
	LOG_LEVEL max_loglvl = LOG_LEVEL::TRACE;

	/**************************************************************************
	 * Since multiple accesses to many different files can overload the open
	 * file descriptors. it is necessary to encapsulate these file descriptors.
	 * An open file is stored in a local map
	 * This file-handle is handling a native file descriptor until the limit
	 * max_native_fds is reached. above this limit, a file is opened for every
	 * use and reopened again upon access.
	 * This enables to have more open files than the kernel supports without
	 * raising the native limits.
	 */
protected:
	/**
	 * Represents an open file.
	 */
	struct open_file_t {
		std::string path;
		bool is_open;
		bool has_changed;
		enum { FILE, DIRECTORY, UNSPEC } type;
		int flags;
		union {
			int32_t fd;
			DIR *dp;
		} fh;
	};
private:
	// The list of open files - and our internal file descriptors.
	// It is not supported generally to use 64 bit fds.
	std::unordered_map<int64_t, open_file_t> open_files;

	// Has to be locked whenever open_files is accessed
	std::mutex open_file_mux;

	// File ids are handed out incrementingly, and this is the counter
	int64_t open_file_count = 1;

	// The maximum number of files that are to be represented natively.
	// If the size of open_files reaches the limit, newly allocated files
	// are stored temporarily
	size_t max_native_fds;

protected:
	class open_file_handle_t {
		friend class Module;
		open_file_t *file;
		const bool should_close;
		open_file_handle_t(open_file_t *, bool should_close);
	public:
		open_file_handle_t(open_file_handle_t &&rhs);
		bool changed() {
			return this->file->has_changed;
		}
		int fd();
		DIR *dp();

		void debug(std::ostream &os);
		~open_file_handle_t();
	};

	/**
	 * will generate an open_file_handle for the given path.
	 *
	 * The path has to exist on the filesystem, so it has to be translated
	 * beforehand.
	 * This has to be used whenever a file handle is required.
	 */
	open_file_handle_t file(const std::string &, fuse_file_info *fi);

	// TODO: This has to be called in rename
	void close_file(const char *path, fuse_file_info *fi);
	void close_file(const std::string &path, fuse_file_info *fi);

	void dump_open_files(std::ostream &);
};

} // mammutfs
