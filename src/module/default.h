#pragma once

#include "../module.h"

namespace mammutfs {

class Default : public Module {
public:
	Default(std::shared_ptr<MammutConfig> config) :
		Module("default", config) {}

	int translatepath(const std::string &path, std::string &out) override {
		size_t slash = path.find("/", 1);
		if (slash == std::string::npos) {
			out = slash;
		} else {
			out = std::string(path, slash);
		}
		return 0;
	}

	bool visible_in_root() override { return false; }

	int getattr(const char *path, struct stat *statbuf) override {
		if (strcmp(path, "/") == 0) {
			/*int retval = */Module::getattr(path, statbuf);
			// Owner for / needs to be root - else chroot for ftp and friends wont work.
			statbuf->st_uid         = 0; //config->anon_uid;
			statbuf->st_gid         = 0; //config->anon_gid;
			return 0;
		}
		this->trace("default::getattr: FAILED!", path);
		return -ENOENT;
	}

	int access(const char *path, int mask) override {
		this->trace("default::access", path);
		if ((mask & W_OK) == W_OK) {
			return -1;
		} else {
			return 0;
		}
	}

	int opendir(const char *path, struct fuse_file_info *fi) override {
		this->trace("default::opendir", path);
		fi->fh = -1;
		return 0;
	}

	int readdir(const char *path,
	            void *buf,
	            fuse_fill_dir_t filler,
	            off_t offset,
	            struct fuse_file_info *fi) override {
		(void) path;
		(void) offset;
		(void) fi;

		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		for (const auto &i : config->resolver->activatedModules()) {
			if (i.second->visible_in_root()) {
				filler(buf, i.first.c_str(), NULL, 0);
			}
		}
		return 0;
	}

	int statfs(const char *path, struct statvfs *statv) {
		this->trace("default::statfs", path);
         /*  struct statfs {
               __fsword_t f_type;    // Type of filesystem (see below)
               __fsword_t f_bsize;   // Optimal transfer block size
               fsblkcnt_t f_blocks;  // Total data blocks in filesystem
               fsblkcnt_t f_bfree;   // Free blocks in filesystem
               fsblkcnt_t f_bavail;  // Free blocks available to unprivileged user
               fsfilcnt_t f_files;   // Total file nodes in filesystem
               fsfilcnt_t f_ffree;   // Free file nodes in filesystem
               fsid_t     f_fsid;    // Filesystem ID
               __fsword_t f_namelen; // Maximum length of filenames
               __fsword_t f_frsize;  // Fragment size (since Linux 2.6)
               __fsword_t f_flags;   // Mount flags of filesystem (since Linux 2.6.36)
               __fsword_t f_spare[xxx]; // Padding bytes reserved for future use
           };
		*/
		int retstat = 0;
		std::string translated;
		if ((retstat = this->translatepath(path, translated))) {
			return retstat;
		}

		// get stats for underlying filesystem
		retstat = ::statvfs(translated.c_str(), statv);
		if (retstat < 0) {
			retstat = -errno;
			this->log(LOG_LEVEL::WRN, "mammut_statfs statvfs");
		}

		return retstat;
	}

	int releasedir(const char *, struct fuse_file_info *) override {
		return 0;
	}
};

}
