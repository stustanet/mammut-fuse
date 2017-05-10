#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {

class PublicAnonLister : public Module {
public:
	PublicAnonLister (std::shared_ptr<MammutConfig> config, std::shared_ptr<Communicator> comm) :
		Module("lister", config),
		comm(comm) {
		comm->register_command("RESET", [this](const std::string &) {
				// todo clean cache
				return true;
			});

	}

	int translatepath(const std::string &path, std::string &out) override {
		if (path == "/") {
			out = ".";
			return 0;
		}

		return Module::translatepath(path.c_str(), out);
		//The Magic is important here/
	}

	int mkdir(const char *path, mode_t mode) override {
		if (strcmp(path, "/") == 0) {
			this->trace("lister::mkdir", path);
			return -EPERM;
		} else {
			// Check if need to add to lister.

			mode = ((mode & 0770) | 0005);
			return Module::mkdir(path, mode); // Maybe test, if this is allowed and
			// return -EPERM early.
		}
	}

	int opendir(const char *path, struct fuse_file_info *fi) override {
		if (strcmp(path, "/") == 0) {
			this->trace("lister::opendir", path);
			return 0;
		} else {
			return Module::opendir(path, fi);
		}
	}

	int readdir(const char *path,
	                   void *buf,
	                   fuse_fill_dir_t filler,
	                   off_t offset,
	                   struct fuse_file_info *fi) override {
		//todo load shared listing

		this->trace("lister::readdir", path);
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);


	}

private:
	std::shared_ptr<Communicator> comm;
};

}
