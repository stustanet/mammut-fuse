#pragma once

#include "../module.h"
#include <unistd.h>

namespace mammutfs {

class Default : public Module {
public:
	Default(const std::shared_ptr<MammutConfig> &config,
	        const std::shared_ptr<Communicator> &comm) :
		Module("default", config, comm) {}

	int translatepath(const std::string &path, std::string &/*out*/) override {
		// It should not happen that translatepath is called - all module::*
		// functions will do this, so check the log, which one it was and
		// implement it here - doing some sensible stuff!
		this->error(0, "default::translatepath",
		            "An operation called translatepath for the default path!",
		            path);
		return -ENOTSUP;
	}

	bool visible_in_root() override { return false; }

	int getattr(const char *path, struct stat *statbuf) override {
#ifdef TRACE_GETATTR
		this->trace("default::getattr", path);
#endif
		if (strcmp(path, "/") == 0) {
			/*int retval = */Module::getattr(path, statbuf);
			// Owner for / needs to be root - else chroot for ftp and friends wont work.
			statbuf->st_uid         = 0; //config->anon_uid;
			statbuf->st_gid         = 0; //config->anon_gid;
			statbuf->st_mode        = S_IFDIR | 0555;  // Protection
			return 0;
		}
		this->trace("default::getattr: FAILED trying to access non-root path!", path);
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
	            off_t /*offset*/,
	            struct fuse_file_info */*fi*/) override {
		this->trace("default::readdir", path);

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
		const std::string &first_raid = this->config->get_first_raid();
		int rc = ::statvfs(first_raid.c_str(), statv);
		if(rc < 0 && errno == ENOENT) {
			return rc;
		}
		return 0;
	}

	int releasedir(const char *path, struct fuse_file_info *) override {
		this->trace("default::closedir", path);
		return 0;
	}
};

}
