#define FUSE_USE_VERSION 26

#include "mammut_fuse.h"

#include "mammut_config.h"

#include <vector>
#include <iostream>

#include <sys/prctl.h>

#include <syslog.h>
#include <unistd.h>
// Forward declare to create the main mount point
extern void setup_main();

namespace mammutfs {

static struct userdata_t {
	std::shared_ptr<ModuleResolver> resolver;
	std::shared_ptr<MammutConfig> config;
} userdata;

#define GETMODULE(path) \
	const char *subdir; \
	Module *module = userdata.resolver->getModuleFromPath(path, subdir); \
	if (module == NULL) { return -ENOENT; }


static int mammut_getattr(const char *path, struct stat *statbuf) {
	GETMODULE(path);
	return module->getattr(subdir, statbuf);
}

static int mammut_readlink(const char *path, char *link, size_t size) {
	(void)path;
	(void)link;
	(void)size;
	// todo - is this right to disable all?
	//GETMODULE(path);
	//module->readlink(subdir, link, size);
	return -ENOTSUP;
}

static int mammut_mkdir(const char *path, mode_t mode) {
	GETMODULE(path);
	return module->mkdir(subdir, mode);
}

static int mammut_unlink(const char *path) {
	GETMODULE(path);
	return module->unlink(subdir);
}

static int mammut_rmdir(const char *path) {
	GETMODULE(path);
	return module->rmdir(subdir);
}

static int mammut_symlink(const char *path, const char *link) {
	(void)path;
	(void)link;
	// todo is it right to disable it completely?
	//GETMODULE(path);
	//return module->symlink(subdir, link);

	return -ENOTSUP;
}

static int mammut_rename(const char *path, const char *newpath) {
	std::string from_translated;
	{
		GETMODULE(path);
		int retval = module->translatepath(subdir, from_translated);
		if (retval) {
			std::cout << "Could not translate from path (" << path << "), " << subdir << std::endl;
			return retval;
		}
	}

	GETMODULE(newpath);
	return module->rename(from_translated.c_str(), subdir, path, newpath);
}

static int mammut_link(const char *path, const char *newpath) {
	(void)path;
	(void)newpath;

	return -ENOTSUP;
}

static int mammut_chmod(const char *path, mode_t mode) {
	GETMODULE(path);
	return module->chmod(subdir, mode);
}

static int mammut_chown(const char *path, uid_t uid, gid_t gid) {
	GETMODULE(path);
	return module->chown(subdir, uid, gid);
}

static int mammut_truncate(const char *path, off_t newsize) {
	GETMODULE(path);
	return module->truncate(subdir, newsize);
}

static int mammut_open(const char *path, struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->open(subdir, fi);
}

static int mammut_read(const char *path,
                       char *buf,
                       size_t size,
                       off_t offset,
                       struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->read(subdir, buf, size, offset, fi);
}

static int mammut_write(const char *path,
                        const char *buf,
                        size_t size,
                        off_t offset,
                        struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->write(subdir, buf, size, offset, fi);
}

static int mammut_statfs(const char *path, struct statvfs *statv) {
	GETMODULE(path);
	return module->statfs(subdir, statv);
}

static int mammut_flush(const char *path, struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->flush(subdir, fi);
}

static int mammut_release(const char *path, struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->release(subdir, fi);
}

static int mammut_fsync(const char *path,
                        int datasync,
                        struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->fsync(subdir, datasync, fi);
}

static int mammut_setxattr(const char *path,
                           const char *name,
                           const char *value,
                           size_t size,
                           int flags) {
	GETMODULE(path);
	return module->setxattr(subdir, name, value, size, flags);
}

static int mammut_getxattr(const char *path,
                           const char *name,
                           char *value,
                           size_t size) {
	GETMODULE(path);
	return module->getxattr(subdir, name, value, size);
}

static int mammut_listxattr(const char *path, char *list, size_t size) {
	GETMODULE(path);
	return module->listxattr(subdir, list, size);
}

static int mammut_removexattr(const char *path, const char *name) {
	GETMODULE(path);
	return module->removexattr(subdir, name);
}

static int mammut_opendir(const char *path, struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->opendir(subdir, fi);
}

static int mammut_readdir(const char *path,
                          void *buf,
                          fuse_fill_dir_t filler,
                          off_t offset,
                          struct fuse_file_info *fi) {
	GETMODULE(path)
	return module->readdir(subdir, buf, filler, offset, fi);
}

static int mammut_releasedir(const char *path, struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->releasedir(subdir, fi);
}

static int mammut_fsyncdir(const char *path,
                           int datasync,
                           struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->fsyncdir(subdir, datasync, fi);
}


static int mammut_access(const char *path, int mask) {
	GETMODULE(path);
	return module->access(subdir, mask);
}

static int mammut_create(const char *path,
                         mode_t mode,
                         struct fuse_file_info *fi) {
	GETMODULE(path);
	return module->create(subdir, mode, fi);
}

static int mammut_utimens(const char *path, const struct timespec tv[2]) {
	GETMODULE(path);
	return module->utimens(subdir, tv);
}

void *mammut_init(struct fuse_conn_info *conn) {
	prctl(PR_SET_NAME, "mammutfs_fuse", 0, 0, 0);
	if (conn->capable & FUSE_CAP_EXPORT_SUPPORT) {
		conn->want |= FUSE_CAP_EXPORT_SUPPORT;
	} else {
		syslog(LOG_ERR, "ERROR NOT SETTING FUSE_CAP_EXPORT_SUPPORT");
	}

	setup_main();

	return &userdata;
}

void mammut_destroy(void *userdata) {
	(void)userdata;
}

#undef GETMODULE

int mammut_main (std::shared_ptr<ModuleResolver> resolver,
                 std::shared_ptr<MammutConfig> config) {
	prctl(PR_SET_NAME, "mammutfs_main", 0, 0, 0);
	openlog("mammutfs", LOG_PID, 0);

	userdata.resolver = resolver;
	userdata.config = config;

	std::vector<const char *> fuseargs;
	//fuseargs.push_back(config->self);
	//fuseargs.push_back("mammutfs");

	fuseargs.push_back("-ofsname=mammutfs");     // We want to have the name mammutfs
	fuseargs.push_back("-osubtype=fuse.mammutfs");  // We want to have the name mammutfs
	fuseargs.push_back("-ononempty");            // We need to mount over the original mount-fuckups
	fuseargs.push_back("-odefault_permissions"); // To allow us to set permissions
	fuseargs.push_back("-oallow_other");         // To enable smb
	fuseargs.push_back("-ouse_ino");             // Copy the underlying inodes instead of giving us new ones. Might give us more inodes!
	// fuseargs.push_back("-onoforget");            // Do not forget inodes. keep them forever
	                                               //- this might be enabled, if nfs is making troubles!
	//fuseargs.push_back("-d");                    // Enable FUSE-DEBUG!
	fuseargs.push_back("-obig_writes");            // HUGHE PERFORMANCE IMPACT! now at ceph level

	if (!config->deamonize()) {
		fuseargs.push_back("-f");
	}

	fuseargs.push_back("--");
	std::string mountpoint = config->mountpoint();
	fuseargs.push_back(mountpoint.c_str());

	std::cout << "fuse args: ";
	for (auto arg : fuseargs) {
		std::cout << " " << arg;
	}
	std::cout << std::endl;

	struct fuse_operations mammut_ops;
	memset(&mammut_ops, 0, sizeof(mammut_ops));
	mammut_ops.getattr  = mammut_getattr;
	mammut_ops.readlink = mammut_readlink;
	mammut_ops.getdir   = NULL;
	mammut_ops.mknod    = NULL;
	mammut_ops.mkdir    = mammut_mkdir;
	mammut_ops.unlink   = mammut_unlink;
	mammut_ops.rmdir    = mammut_rmdir;
	mammut_ops.symlink  = mammut_symlink;
	mammut_ops.rename   = mammut_rename;
	mammut_ops.link     = mammut_link;
	mammut_ops.chmod    = mammut_chmod;
	mammut_ops.chown    = mammut_chown;
	mammut_ops.truncate = mammut_truncate;
	mammut_ops.open     = mammut_open;
	mammut_ops.read     = mammut_read;
	mammut_ops.write    = mammut_write;

	mammut_ops.statfs  = mammut_statfs;
	mammut_ops.flush   = mammut_flush;
	mammut_ops.release = mammut_release;
	mammut_ops.fsync   = mammut_fsync;

	mammut_ops.setxattr    = mammut_setxattr;
	mammut_ops.getxattr    = mammut_getxattr;
	mammut_ops.listxattr   = mammut_listxattr;
	mammut_ops.removexattr = mammut_removexattr;

	mammut_ops.opendir    = mammut_opendir;
	mammut_ops.readdir    = mammut_readdir;
	mammut_ops.releasedir = mammut_releasedir;
	mammut_ops.fsyncdir   = mammut_fsyncdir;
	mammut_ops.init       = mammut_init;
	mammut_ops.destroy    = mammut_destroy;
	mammut_ops.access     = mammut_access;
	mammut_ops.create     = mammut_create;
	mammut_ops.utimens    = mammut_utimens;

	// the magic happens here
	int fuse_stat = fuse_main(
		fuseargs.size(),
		const_cast<char**>(fuseargs.data()),
		&mammut_ops,
		&userdata);

	if (fuse_stat) {
		std::stringstream ss;
		ss << "fuse failed: " << strerror(errno);
		if (errno == EPERM) {
			ss << " user " << config->username() << " needs write access to " << config->mountpoint();
		}
		syslog(LOG_ERR, ss.str().c_str());
		std::cerr << ss.str();
	}
	return fuse_stat;
}

}
