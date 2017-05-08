#include "module.h"

#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <string.h>

#include <iostream>

namespace mammutfs {

Module::Module(const std::string &modname, std::shared_ptr<MammutConfig> config) :
	config(config), modname(modname) {
	trace ("tomate", "test");
}


int Module::translatepath(const std::string &path, std::string &out) {
	std::string basepath;
	int retval = find_raid(basepath);

	out = basepath + path;
	std::cout << "PATH: " << out << std::endl;
	return retval;
}

int Module::find_raid(std::string &path) {
	if (basepath != "") {
		path = basepath;
		return 0;
	}

	for (const auto &raid : config->raids) {
		std::string to_test = raid + "/" + modname + "/" + config->username;

		this->log(LOG_LEVEL::INFO, std::string("Testing raid " + to_test));
		struct stat statbuf;
		int retval = stat(to_test.c_str(), &statbuf);

		if (retval == 0) {
			this->log(LOG_LEVEL::INFO, std::string("Found raid at " + to_test));
			basepath = to_test;
			break;
		}
	}
	if (basepath == "") {
		this->log(LOG_LEVEL::ERR, "Could not find Raid!! THIS IS BAD!");
		return -ENOENT;
	}

	path = basepath;
	return 0;
}

void Module::log(LOG_LEVEL lvl, const std::string &msg) {
	std::cout << "[" << modname << "] " << msg << std::endl;
}

void Module::trace(const std::string &method,
                   const std::string &path,
                   const std::string &second_path) {
	log(LOG_LEVEL::TRACE, method + " " + path + " " + second_path);
}

int Module::getattr(const char *path, struct stat *statbuf) {
	this->trace("getattr", path);

	if (strcmp(path, "/") == 0) {
		statbuf->st_dev         = 0;               // IGNORED Device
		statbuf->st_ino         = 999;             // IGNORED inode number
		statbuf->st_mode        = S_IFDIR | 0755;  // Protection
		statbuf->st_nlink       = 0;               // Number of Hard links
		statbuf->st_uid         = config->user_uid;
		statbuf->st_gid         = config->user_gid;
		statbuf->st_rdev        = 0;
		statbuf->st_size        = 0;
		statbuf->st_blksize     = 0;  // IGNORED
		statbuf->st_blocks      = 0;
		statbuf->st_atim.tv_sec = 0;  // Last Access
		statbuf->st_mtim.tv_sec = 0;  // Last Modification
		statbuf->st_ctim.tv_sec = 0;  // Last Status change
		return 0;
	}

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->log(LOG_LEVEL::WRN, "translatepath lstat\n");
	} else {
		retstat = lstat(translated.c_str(), statbuf);
		if (retstat) {
			retstat = -errno;
			this->log(LOG_LEVEL::WRN, "mammut_getattr lstat\n");
		}
	}

	// Eliminate all User-IDs from the items
	statbuf->st_uid = config->anon_uid;
	statbuf->st_gid = config->anon_gid;

	return retstat;
}


int Module::readlink(const char *path, char *link, size_t size) {
	this->trace("readlink", path);
	int retstat = 0;

	if (size == 0)
		return -EINVAL;

	std::string translated;
	retstat = this->translatepath(path, translated);

	retstat = readlink(translated.c_str(), link, size - 1);
	if (retstat < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_unlink unlink");
	} else {
		link[retstat] = '\0';
		retstat       = 0;
	}

	return retstat;
}

/* Deprecated, use readdir() instead */
int Module::getdir(const char *path, fuse_dirh_t, fuse_dirfil_t) {
	this->trace("getdir", path);
	return -ENOTSUP;
}


int Module::mknod(const char *path, mode_t, dev_t) {
	this->trace("mknod", path);
	return -ENOTSUP;
}


int Module::mkdir(const char *path, mode_t mode) {
	this->trace("mkdir", path);
	
	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	if ((retstat = mkdir(translated.c_str(), mode)) < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_mkdir mkdir");
	}

	return retstat;
}


int Module::unlink(const char *path) {
	this->trace("unlink", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	if ((retstat = unlink(translated.c_str()))) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_unlink unlink");
	}

	return retstat;
}


int Module::rmdir(const char *path) {
	this->trace("rmdir", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	if ((retstat = rmdir(translated.c_str()))) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_unlink unlink");
	}

	return retstat;
}


int Module::symlink(const char *path, const char *) {
	this->trace("symlink", path);

	return -ENOTSUP;
}


int Module::rename(const char *realpath, const char *newpath) {
	this->trace("rename", realpath, newpath);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(newpath, translated))) {
		return retstat;
	}

	if ((retstat = rename(realpath, translated.c_str())) < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_rename rename");
	}
	return retstat;
}


int Module::chmod(const char *path, mode_t mode) {
	this->trace("chmod", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	if ((retstat = chmod(translated.c_str(), mode)) < 0) {;
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_chmod chmod");
	}

	return retstat;
}


int Module::chown(const char *path, uid_t, gid_t) {
	this->trace("chown", path);

	return -EPERM;
}


int Module::truncate(const char *path, off_t newsize) {
	this->trace("truncate", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	if (newsize > config->truncate_max_size) {
		struct stat st;
		if (stat(translated.c_str(), &st) != 0) {
			return -errno;
		}

		if (st.st_size < newsize) {
			return -EPERM;
		}
	}

	retstat = truncate(translated.c_str(), newsize);
	if (retstat < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_truncate truncate");
	}

	return retstat;
}

int Module::open(const char *path, struct fuse_file_info *fi) {
	this->trace("open", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	int fd = ::open(translated.c_str(), fi->flags);
	if (fd < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_open open");
	}

	// Store fd in fh, a user defined value;
	fi->fh = fd;

	return retstat;
}


int Module::read(const char *path, char *buf, size_t size, off_t offset,
         struct fuse_file_info *fi) {
	this->trace("read", path);

	// Take fd from fi->fh
	int retstat = pread(fi->fh, buf, size, offset);
	if (retstat < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_read read");
	}

	return retstat;
}


int Module::write(const char *path, const char *buf, size_t size, off_t offset,
          struct fuse_file_info *fi) {
	this->trace("write", path);

	// Take fd from fi->fh
	int retstat = pwrite(fi->fh, buf, size, offset);
	if (retstat < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_write write");
	}

	return retstat;
}


int Module::statfs(const char *path, struct statvfs *statv) {
	this->trace("statfs", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	// get stats for underlying filesystem
	retstat = statvfs(translated.c_str(), statv);
	if (retstat < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_statfs statvfs");
	}

	return retstat;
}


int Module::flush(const char *path, struct fuse_file_info *) {
	this->trace("flush", path);

	return 0;
}


int Module::release(const char *path, struct fuse_file_info *fi) {
	this->trace("release", path);
	(void)path;
	int retstat = 0;

	// We need to close the file. Had we allocated any resources
	// (buffers etc) we'd need to free them here as well.
	retstat = close(fi->fh);

	return retstat;
}


int Module::fsync(const char *path, int, struct fuse_file_info *fi) {
	this->trace("fsync", path);

	int retstat = 0;
	retstat = ::fsync(fi->fh);

	if (retstat < 0) {
		errno = -retstat;
		this->log(LOG_LEVEL::WRN, "mammut_fsync fsync");
	}

	return retstat;
}


int Module::setxattr(const char *path, const char *, const char *, size_t, int) {
	this->trace("setxattr", path);
	return -ENOTSUP;
}

/** Get extended attributes */
int Module::getxattr(const char *path, const char *, char *, size_t) {
	this->trace("getxattr", path);
	return -ENOTSUP;
}

/** List extended attributes */
int Module::listxattr(const char *path, char *, size_t) {
	this->trace("listxattr", path);
	return -ENOTSUP;
}

/** Remove extended attributes */
int Module::removexattr(const char *path, const char *) {
	this->trace("removexattr", path);
	return -ENOTSUP;
}


int Module::opendir(const char *path, struct fuse_file_info *fi) {
	this->trace("opendir", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}


	DIR *dp = ::opendir(translated.c_str());
	if (dp == NULL) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_opendir opendir");
		return retstat;
	}

	fi->fh = reinterpret_cast<intptr_t>(dp);
	return 0;
}


int Module::readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
            struct fuse_file_info *fi) {
	this->trace("readdir", path);
	(void)offset;

	DIR *dp = reinterpret_cast<DIR *>(fi->fh);

	if (dp == 0) {
		return -EINVAL;
	}

	rewinddir(dp);

	struct dirent *de;
	while ((de = ::readdir(dp)) != NULL) {
		if (filler(buf, de->d_name, NULL, 0) != 0)
			return -ENOMEM;
	}

	return 0;
}


int Module::releasedir(const char *path, struct fuse_file_info *fi) {
	this->trace("releasedir", path);
	int retstat = 0;

	DIR *dp = reinterpret_cast<DIR *>(fi->fh);
	closedir(dp);

	return retstat;
}


int Module::fsyncdir(const char *path, int, struct fuse_file_info *) {
	this->trace("fsyncdir", path);

	return 0;
}


int Module::access(const char *path, int mask) {
	this->trace("access", path);
	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	return ::access(translated.c_str(), mask);
}


int Module::create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	this->trace("access", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	int fd = ::creat(translated.c_str(), mode);
	if (fd < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_create creat");
	}

	// Store the fd in user defined storage in fi
	fi->fh = fd;

	return retstat;
}


int Module::utimens(const char *path, const struct timespec tv[2]) {
	this->trace("utimens", path);
	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		return retstat;
	}

	retstat = ::utimensat(0, translated.c_str(), tv, 0);
	if (retstat < 0) {
		retstat = -errno;
		this->log(LOG_LEVEL::WRN, "mammut_utime utime");
	}

	return retstat;
}

}
