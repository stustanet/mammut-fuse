#pragma once

#include "../module.h"

namespace mammutfs {

class Default : public Module {
public:
	Default(std::shared_ptr<MammutConfig> config) :
		Module(config) {}
	int translatepath(const std::string &path, std::string &out) override {
		size_t slash = path.find("/", 1);
		if (slash == std::string::npos) {
			out = slash;
		} else {
			out = std::string(path, slash);
		}
		return 0;
	}

	std::string find_raid(const std::string &user,
	                      const std::string &module) override {
		return "";
	}

	bool visible_in_root() override { return false; }

	int getattr(const char *path, struct stat *statbuf) override {
		this->trace("Default:getattr", path);
		if (strcmp(path, "/") == 0) {
			statbuf->st_dev         = 0;               // IGNORED Device
			statbuf->st_ino         = 999;             // IGNORED inode number
			statbuf->st_mode        = S_IFDIR | 0755;  // Protection
			statbuf->st_nlink       = 0;               // Number of Hard links
			statbuf->st_uid         = config->anon_uid;
			statbuf->st_gid         = config->anon_gid;
			statbuf->st_rdev        = 0;
			statbuf->st_size        = 0;
			statbuf->st_blksize     = 0;  // IGNORED
			statbuf->st_blocks      = 0;
			statbuf->st_atim.tv_sec = 0;  // Last Access
			statbuf->st_mtim.tv_sec = 0;  // Last Modification
			statbuf->st_ctim.tv_sec = 0;  // Last Status change
			return 0;
		}
		return -ENOENT;
	}

	
	int access(const char *path, int mask) override {
		if ((mask & W_OK) == W_OK) {
			return -1;
		} else {
			return 0;
		}
	}

	int opendir(const char *path, struct fuse_file_info *fi) override {
		fi->fh = -1;
		return 0;
	}

	int readdir(const char *path,
	            void *buf,
	            fuse_fill_dir_t filler,
	            off_t offset,
	            struct fuse_file_info *fi) override {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		for (const auto &i : config->resolver->activatedModules()) {
			if (i.second->visible_in_root()) {
				filler(buf, i.first.c_str(), NULL, 0);
			}
		}
		return 0;
	}

	int releasedir(const char *, struct fuse_file_info *) override {
		return 0;
	}
};

}
