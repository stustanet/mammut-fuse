/*
   Big Brother File System
   Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

   This program can be distributed under the terms of the GNU GPLv3.
   See the file COPYING.

   This code is derived from function prototypes found /usr/include/fuse/fuse.h
   Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
   His code is licensed under the LGPLv2.
   A copy of that code is included in the file fuse.h

   The point of this FUSE filesystem is to provide an introduction to
   FUSE.  It was my first FUSE filesystem as I got to know the
   software; hopefully, the comments in this code will help people who
   follow later to get a gentler introduction.

   This might be called a no-op filesystem:  it doesn't impose
   filesystem semantics on top of any other existing structure.  It
   simply reports the requests that come in, and passes them to an
   underlying filesystem.  The information is saved in a logfile named
   bbfs.log, in the directory from which you run bbfs.

   gcc -Wall `pkg-config fuse --cflags --libs` -o bbfs bbfs.c
   */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

enum MAMMUT_PATH_MODE {
	MODE_HOMEDIR,
	MODE_LISTDIR_PUBLIC,
	MODE_LISTDIR_ANON, 
	MODE_PIPETHROUGH_ANON, 
	MODE_PIPETHROUGH_RO,
	MODE_PIPETHROUGH_RW
};

// Report errors to logfile and give -errno to caller
static int mammut_error(const char *str)
{
	int ret = -errno;
	printf("Error %s\n", str);
	return ret;
}

static void _mammut_locate_userdir (char *fpath, const char *userid, const char *subdir)
{
	strcpy(fpath, "/var/raid");
	strcpy(fpath, subdir);
	strcpy(fpath, userid);
	///TODO Locate xfs user filesystem
}

static int mammut_fullpath(char fpath[PATH_MAX], const char *path, enum MAMMUT_PATH_MODE* mode)
{
	//strukutur pfad public/private.../: ./users/USERID/public → /"raid"/public/USERID/
	//strukutur pfad anon pfad: ./export/anon  → Virtuell Anon verzeichnis

	*mode = MODE_HOMEDIR;
	char *my_path = strdup(path);
	char *token;
	char *saveptr;
	char* other_id;

	while (*mode != MODE_PIPETHROUGH_RW &&
	       *mode != MODE_PIPETHROUGH_RO &&
	       *mode != MODE_PIPETHROUGH_ANON &&
	       (token = strtok_r(my_path, "/", &saveptr))) {
		switch(*mode) {
			case MODE_HOMEDIR:
				if (!strcmp(token, "public") || !strcmp(token, "private") ||
					!strcmp(token, "anonymous")) {
					strcpy(fpath, basepath); ///TODO BASEPATH
					if (token)
						strcat(fpath, token);
					*mode = MODE_PIPETHROUGH_RW;
				} else if (!strcmp(token, "list-anon")) {
					*mode = MODE_LISTDIR_ANON;
				} else if (!strcmp(token, "list-public")) {
					*mode = MODE_LISTDIR_PUBLIC;
				} else {
					return -42;
				}
				break;
			case MODE_LISTDIR_PUBLIC:
				other_id = token;
				*mode = MODE_PIPETHROUGH_RO;
				_mammut_locate_userdir(fpath, other_id, "public");
				break;
			case MODE_LISTDIR_ANON:
				///TODO Subdirecotory of anon: userid
				other_id = token; ///TODO Locate other ID
				*mode = MODE_PIPETHROUGH_ANON;
				_mammut_locate_userdir(fpath, other_id, "anonymous");
				break;
			default: break;
		}
	}
	return 0;
}

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//
/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int mammut_getattr(const char *path, struct stat *statbuf)
{
	int retstat = 0;
	char fpath[PATH_MAX];

	enum MAMMUT_PATH_MODE mode;
	mammut_fullpath(fpath, path, &mode);

	if ((retstat = lstat(fpath, statbuf)))
		retstat = mammut_error("mammut_getattr lstat");

	if (mode == MODE_PIPETHROUGH_RW)
		return retstat;

	if (mode == MODE_PIPETHROUGH_ANON)
		// Eliminate all User-IDs from the file
		statbuf->st_uid = 1000; ///TODO which user?

	if (S_ISREG(statbuf->st_mode))
		statbuf->st_mode |= 0004;
	else if (S_ISDIR(statbuf->st_mode))
		statbuf->st_mode |= 0005;

	return retstat;
}

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.  If the linkname is too long to fit in the
 * buffer, it should be truncated.  The return value should be 0
 * for success.
 */
// Note the system readlink() will truncate and lose the terminating
// null.  So, the size passed to to the system readlink() must be one
// less than the size passed to mammut_readlink()
// mammut_readlink() code by Bernardo F Costa (thanks!)
int mammut_readlink(const char *path, char *link, size_t size)
{
	(void)path; 
	(void)link; 
	(void)size;

	mammut_error("Not Implemented Yet"); 
	return -43; 
/*
	int retstat = 0;
	char fpath[PATH_MAX];

	enum MAMMUT_PATH_MODE mode;
	mammut_fullpath(fpath, path, &mode);

	retstat = readlink(fpath, link, size - 1);
	if (retstat < 0)
		retstat = mammut_error("mammut_readlink readlink");
	else  {
		link[retstat] = '\0';
		retstat = 0;
	}

	return retstat;
*/
}

/** Create a file node
 *
 * There is no create() operation, mknod() will be called for
 * creation of all non-directory, non-symlink nodes.
 */
// shouldn't that comment be "if" there is no.... ?
int mammut_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void)path; 
	(void)mode;
	(void)dev;

	return -44; 
	/*
	int retstat = 0;
	char fpath[PATH_MAX];
	

	MAMMUT_PATH_MODE mmode; 
	mammut_fullpath(fpath, path, &mmode);

	// On Linux this could just be 'mknod(path, mode, rdev)' but this
	//  is more portable
	if (S_ISREG(mode)) {
		retstat = open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (retstat < 0)
			retstat = mammut_error("mammut_mknod open");
		else {
			retstat = close(retstat);
			if (retstat < 0)
				retstat = mammut_error("mammut_mknod close");
		}
	} else
		if (S_ISFIFO(mode)) {
			retstat = mkfifo(fpath, mode);
			if (retstat < 0)
				retstat = mammut_error("mammut_mknod mkfifo");
		} else {
			retstat = mknod(fpath, mode, dev);
			if (retstat < 0)
				retstat = mammut_error("mammut_mknod mknod");
		}

	return retstat;
	*/
}

/** Create a directory */
int mammut_mkdir(const char *path, mode_t mode)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	
	MAMMUT_PATH_MODE mammut_mode; 
	mammut_fullpath(fpath, path, &mammut_mode);

	if (mammut_mode != MODE_PIPETHROUGH_RW)
		return -45; 

	retstat = mkdir(fpath, mode);
	if (retstat < 0)
		retstat = mammut_error("mammut_mkdir mkdir");

	return retstat;
}

/** Remove a file */
int mammut_unlink(const char *path)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	
	MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	if (mode != MODE_PIPETHROUGH_RW)
		return -46; 

	retstat = unlink(fpath);
	if (retstat < 0)
		retstat = mammut_error("mammut_unlink unlink");

	return retstat;
}

/** Remove a directory */
int mammut_rmdir(const char *path)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	
	MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	if (mode != MODE_PIPETHROUGH_RW)
		return -47;

	retstat = rmdir(fpath);
	if (retstat < 0)
		retstat = mammut_error("mammut_rmdir rmdir");

	return retstat;
}

/** Create a symbolic link */
// The parameters here are a little bit confusing, but do correspond
// to the symlink() system call.  The 'path' is where the link points,
// while the 'link' is the link itself.  So we need to leave the path
// unaltered, but insert the link into the mounted directory.
int mammut_symlink(const char *path, const char *link)
{
	int retstat;
	char flink[PATH_MAX];
	
	MAMMUT_PATH_MODE mode; 
	mammut_fullpath(flink, link, &mode);
	
	if (mode != MODE_PIPETHROUGH_RW) 
		return -48;

	retstat = symlink(path, flink);
	if (retstat < 0)
		retstat = mammut_error("mammut_symlink symlink");

	return retstat;
}

/** Rename a file */
// both path and newpath are fs-relative
int mammut_rename(const char *path, const char *newpath)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	char fnewpath[PATH_MAX];
	
	MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	if (mode != MODE_PIPETHROUGH_RW)
		return -49;

	mammut_fullpath(fnewpath, newpath, &mode);

	if (mode != MODE_PIPETHROUGH_RW) 
		return -50; 

	retstat = rename(fpath, fnewpath);
	if (retstat < 0)
		retstat = mammut_error("mammut_rename rename");

	return retstat;
}

/** Create a hard link to a file */
int mammut_link(const char *path, const char *newpath)
{
	
	return -51; 
	/*
	int retstat = 0;
	char fpath[PATH_MAX], fnewpath[PATH_MAX];
	
	mammut_fullpath(fpath, path, &mode);
	mammut_fullpath(fnewpath, newpath);

	retstat = link(fpath, fnewpath);
	if (retstat < 0)
		retstat = mammut_error("mammut_link link");

	return retstat;
	*/
}

/** Change the permission bits of a file */
int mammut_chmod(const char *path, mode_t mode)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	
	MAMMUT_PATH_MODE mammut_mode; 
	mammut_fullpath(fpath, path, &mammut_mode);

	if (mammut_mode != MODE_PIPETHROUGH_RW)
		return -52;

	retstat = chmod(fpath, mode);
	if (retstat < 0)
		retstat = mammut_error("mammut_chmod chmod");

	return retstat;
}

/** Change the owner and group of a file */
int mammut_chown(const char *path, uid_t uid, gid_t gid)
{

	(void)path; 
	(void)uid;
	(void)git; 
	return -53; 

	/*
	int retstat = 0;
	char fpath[PATH_MAX];
	
	mammut_fullpath(fpath, path);

	retstat = chown(fpath, uid, gid);
	if (retstat < 0)
		retstat = mammut_error("mammut_chown chown");

	return retstat;
	*/
}

/** Change the size of a file */
int mammut_truncate(const char *path, off_t newsize)
{
	//TODO Idee: Limit newsize to 10G? 

	(void)path; 
	(void)newsize; 
	return -54; 
	/*
	int retstat = 0;
	char fpath[PATH_MAX];

	mammut_fullpath(fpath, path);

	retstat = truncate(fpath, newsize);
	if (retstat < 0)
		mammut_error("mammut_truncate truncate");

	return retstat; 
	*/
}

/** Change the access and/or modification times of a file */
/* note -- I'll want to change this as soon as 2.6 is in debian testing */
int mammut_utime(const char *path, struct utimbuf *ubuf)
{
	int retstat = 0;
	char fpath[PATH_MAX];

	MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	if (mode != MODE_PIPETHROUGH_RW)
		return -55; 

	retstat = utime(fpath, ubuf);
	if (retstat < 0)
		retstat = mammut_error("mammut_utime utime");

	return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int mammut_open(const char *path, struct fuse_file_info *fi)
{
	int retstat = 0;
	int fd;
	char fpath[PATH_MAX];
	
	MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	if (mode != MODE_PIPETHROUGH_RW && (fi->flags != O_RDONLY)) 
		return -56;

	fd = open(fpath, fi->flags);
	if (fd < 0)
		retstat = mammut_error("mammut_open open");

	fi->fh = fd;

	return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
// I don't fully understand the documentation above -- it doesn't
// match the documentation for the read() system call which says it
// can return with anything up to the amount of data requested. nor
// with the fusexmp code which returns the amount of data also
// returned by read.
int mammut_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void)path; 
	
	int retstat = 0;

	retstat = pread(fi->fh, buf, size, offset);
	if (retstat < 0)
		retstat = mammut_error("mammut_read read");

	return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
// As  with read(), the documentation above is inconsistent with the
// documentation for the write() system call.
int mammut_write(const char *path, const char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	(void) path; 
	int retstat = 0;

	retstat = pwrite(fi->fh, buf, size, offset);
	if (retstat < 0)
		retstat = mammut_error("mammut_write pwrite");

	return retstat;
}

/** Get file system statistics
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 *
 * Replaced 'struct statfs' parameter with 'struct statvfs' in
 * version 2.5
 */
int mammut_statfs(const char *path, struct statvfs *statv)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	
	enum MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	// get stats for underlying filesystem
	retstat = statvfs(fpath, statv);
	if (retstat < 0)
		retstat = mammut_error("mammut_statfs statvfs");

	return retstat;
}

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
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
int mammut_flush(const char *path, struct fuse_file_info *fi)
{
	(void)path;
	(void)fi; 

	int retstat = 0;
	
	return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int mammut_release(const char *path, struct fuse_file_info *fi)
{
	(void) path; 
	int retstat = 0;

	// We need to close the file.  Had we allocated any resources
	// (buffers etc) we'd need to free them here as well.
	retstat = close(fi->fh);

	return retstat;
}

/** Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data.
 *
 * Changed in version 2.2
 */
int mammut_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
	(void)path; 
	(void)datasync;
	
	int retstat = 0;

	// some unix-like systems (notably freebsd) don't have a datasync call
#ifdef HAVE_FDATASYNC
	if (datasync)
		retstat = fdatasync(fi->fh);
	else
#endif	
		retstat = fsync(fi->fh);

	if (retstat < 0)
		mammut_error("mammut_fsync fsync");

	return retstat;
}

#ifdef HAVE_SYS_XATTR_H
/** Set extended attributes */
int mammut_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	
	enum MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path &mode);
	
	if (mode != MODE_PIPETHROUGH_RW)
		return ENOTSUP; 

	retstat = lsetxattr(fpath, name, value, size, flags);
	if (retstat < 0)
		retstat = mammut_error("mammut_setxattr lsetxattr");

	return retstat;
}

/** Get extended attributes */
int mammut_getxattr(const char *path, const char *name, char *value, size_t size)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	
	enum MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	retstat = lgetxattr(fpath, name, value, size);
	if (retstat < 0)
		retstat = mammut_error("mammut_getxattr lgetxattr");
	else

		return retstat;
}

/** List extended attributes */
int mammut_listxattr(const char *path, char *list, size_t size)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	char *ptr;

	enum MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	retstat = llistxattr(fpath, list, size);
	if (retstat < 0)
		retstat = mammut_error("mammut_listxattr llistxattr");

	for (ptr = list; ptr < list + retstat; ptr += strlen(ptr)+1)

		return retstat;
}

/** Remove extended attributes */
int mammut_removexattr(const char *path, const char *name)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	
	enum MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	if(mode != MODE_PIPETHROUGH_RW)
		return ENOTSUP; 

	retstat = lremovexattr(fpath, name);
	if (retstat < 0)
		retstat = mammut_error("mammut_removexattr lrmovexattr");

	return retstat;
}
#endif

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int mammut_opendir(const char *path, struct fuse_file_info *fi)
{
	DIR *dp;
	int retstat = 0;
	char fpath[PATH_MAX];

	enum MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);


	dp = opendir(fpath);
	if (dp == NULL)
		retstat = mammut_error("mammut_opendir opendir");

	fi->fh = (intptr_t) dp;

	return retstat;
}

/** Read directory
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
int mammut_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
		struct fuse_file_info *fi)
{
	(void)path; 
	(void)offset; 

	int retstat = 0;
	DIR *dp;
	struct dirent *de;

	// once again, no need for fullpath -- but note that I need to cast fi->fh
	dp = (DIR *) (uintptr_t) fi->fh;

	// Every directory contains at least two entries: . and ..  If my
	// first call to the system readdir() returns NULL I've got an
	// error; near as I can tell, that's the only condition under
	// which I can get an error from readdir()
	de = readdir(dp);
	if (de == 0) {
		retstat = mammut_error("mammut_readdir readdir");
		return retstat;
	}

	// This will copy the entire directory into the buffer.  The loop exits
	// when either the system readdir() returns NULL, or filler()
	// returns something non-zero.  The first case just means I've
	// read the whole directory; the second means the buffer is full.
	do {
		if (filler(buf, de->d_name, NULL, 0) != 0) {
			return -ENOMEM;
		}
	} while ((de = readdir(dp)) != NULL);

	return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int mammut_releasedir(const char *path, struct fuse_file_info *fi)
{
	(void)path;
	int retstat = 0;

	closedir((DIR *) (uintptr_t) fi->fh);

	return retstat;
}

/** Synchronize directory contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data
 *
 * Introduced in version 2.3
 */
// when exactly is this called?  when a user calls fsync and it
// happens to be a directory? ???
int mammut_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
	(void)path; 
	(void)datasync;
	(void)fi;
	int retstat = 0;

	return retstat;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
// Undocumented but extraordinarily useful fact:  the fuse_context is
// set up before this function is called, and
// fuse_get_context()->private_data returns the user_data passed to
// fuse_main().  Really seems like either it should be a third
// parameter coming in here, or else the fact should be documented
// (and this might as well return void, as it did in older versions of
// FUSE).
void *mammut_init(struct fuse_conn_info *conn)
{
	(void) conn; 
	static int MAMMUT_DATA; 
	return &MAMMUT_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void mammut_destroy(void *userdata)
{
	(void)userdata;
}

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
int mammut_access(const char *path, int mask)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	
	enum MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	retstat = access(fpath, mask);
	
	if (mode != MODE_PIPETHROUGH_RW)
		mask &= ~W_OK; 

	if (retstat < 0)

		return retstat;
}

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
 *//*
int mammut_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int retstat = 0;
	char fpath[PATH_MAX];
	int fd;

	enum MAMMUT_PATH_MODE mode; 
	mammut_fullpath(fpath, path, &mode);

	fd = creat(fpath, mode);
	if (fd < 0)
		retstat = mammut_error("mammut_create creat");

	fi->fh = fd;

	return retstat;
}*/

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the
 * truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 *
 * Introduced in version 2.5
 *//*
int mammut_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
	int retstat = 0;

	retstat = ftruncate(fi->fh, offset);
	if (retstat < 0)
		retstat = mammut_error("mammut_ftruncate ftruncate");

	return retstat;
}*/

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the
 * file information is available.
 *
 * Currently this is only called after the create() method if that
 * is implemented (see above).  Later it may be called for
 * invocations of fstat() too.
 *
 * Introduced in version 2.5
 *//*
int mammut_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
	int retstat = 0;

	// On FreeBSD, trying to do anything with the mountpoint ends up
	// opening it, and then using the FD for an fgetattr.  So in the
	// special case of a path of "/", I need to do a getattr on the
	// underlying root directory instead of doing the fgetattr().
	if (!strcmp(path, "/"))
		return mammut_getattr(path, statbuf);

	retstat = fstat(fi->fh, statbuf);
	if (retstat < 0)
		retstat = mammut_error("mammut_fgetattr fstat");

	return retstat;
}*/

struct fuse_operations mammut_oper = {
	.getattr = mammut_getattr,
	.readlink = mammut_readlink,
	// no .getdir -- that's deprecated
	.getdir = NULL,
	.mknod = mammut_mknod,
	.mkdir = mammut_mkdir,
	.unlink = mammut_unlink,
	.rmdir = mammut_rmdir,
	.symlink = mammut_symlink,
	.rename = mammut_rename,
	.link = mammut_link,
	.chmod = mammut_chmod,
	.chown = mammut_chown,
	.truncate = mammut_truncate,
	.utime = mammut_utime,
	.open = mammut_open,
	.read = mammut_read,
	.write = mammut_write,
	/** Just a placeholder, don't set */ // huh???
	.statfs = mammut_statfs,
	.flush = mammut_flush,
	.release = mammut_release,
	.fsync = mammut_fsync,

#ifdef HAVE_SYS_XATTR_H
	.setxattr = mammut_setxattr,
	.getxattr = mammut_getxattr,
	.listxattr = mammut_listxattr,
	.removexattr = mammut_removexattr,
#endif

	.opendir = mammut_opendir,
	.readdir = mammut_readdir,
	.releasedir = mammut_releasedir,
	.fsyncdir = mammut_fsyncdir,
	.init = mammut_init,
	.destroy = mammut_destroy,
	.access = mammut_access
	//.create = mammut_create,
	//.ftruncate = mammut_ftruncate,
	//.fgetattr = mammut_fgetattr
};

void mammut_usage()
{
	fprintf(stderr, "usage:  bbfs [FUSE and mount options] rootDir mountPoint\n");
	abort();
}

int main(int argc, char *argv[])
{
	int fuse_stat;
	struct mammut_state *mammut_data;

	// bbfs doesn't do any access checking on its own (the comment
	// blocks in fuse.h mention some of the functions that need
	// accesses checked -- but note there are other functions, like
	// chown(), that also need checking!).  Since running bbfs as root
	// will therefore open Metrodome-sized holes in the system
	// security, we'll check if root is trying to mount the filesystem
	// and refuse if it is.  The somewhat smaller hole of an ordinary
	// user doing it with the allow_other flag is still there because
	// I don't want to parse the options string.
	if ((getuid() == 0) || (geteuid() == 0)) {
		fprintf(stderr, "Running BBFS as root opens unnacceptable security holes\n");
		return 1;
	}

	// Perform some sanity checking on the command line:  make sure
	// there are enough arguments, and that neither of the last two
	// start with a hyphen (this will break if you actually have a
	// rootpoint or mountpoint whose name starts with a hyphen, but so
	// will a zillion other programs)
	if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
		mammut_usage();

	mammut_data = malloc(sizeof(struct mammut_state));
	if (mammut_data == NULL) {
		perror("main calloc");
		abort();
	}

	// Pull the rootdir out of the argument list and save it in my
	// internal data
	mammut_data->rootdir = realpath(argv[argc-2], NULL);
	argv[argc-2] = argv[argc-1];
	argv[argc-1] = NULL;
	argc--;


	// turn over control to fuse
	fprintf(stderr, "about to call fuse_main\n");
	fuse_stat = fuse_main(argc, argv, &mammut_oper, mammut_data);
	fprintf(stderr, "fuse_main returned %d\n", fuse_stat);

	return fuse_stat;
}
