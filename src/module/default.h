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
			int retval = Module::getattr(path, statbuf);
			statbuf->st_uid         = config->anon_uid;
			statbuf->st_gid         = config->anon_gid;
			return retval;
		}
		return -ENOENT;
	}

	int access(const char *path, int mask) override {
		(void)path;
		if ((mask & W_OK) == W_OK) {
			return -1;
		} else {
			return 0;
		}
	}

	int opendir(const char *path, struct fuse_file_info *fi) override {
		(void) path;
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

	int releasedir(const char *, struct fuse_file_info *) override {
		return 0;
	}
};

}
