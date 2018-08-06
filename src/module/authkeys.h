#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {

class Authkeys : public Module {
public:
	Authkeys(const std::shared_ptr<MammutConfig> &config,
	         const std::shared_ptr<Communicator> &comm) :
		Module("authkeys", config, comm) {
		// Check if `<RAID>/authkeys/<uid>/authorized_keys` exist

		int retstat = 0;
		std::string translated;
		if ((retstat = translatepath("authorized_keys", translated)) != 0) {
			this->error("Authkeys::<ctor>",
			            "Could not translate authorized keys",
			            "authorized_keys");
			return;
		}
		int fd = ::open(translated.c_str(), O_PATH | O_CREAT, 0600);
		::close(fd);
	}

	int translatepath(const std::string &path, std::string &out) override {
		// todo test
		if (path == "authorized_keys" || path == "/authorized_keys" ||  path == "/") {
			return Module::translatepath(path, out);
		} else {
			return -ENOENT;
		}
	}

	// getattr is also protected by translatepath

	int readlink(const char */*path*/, char */*link*/, size_t /*size*/) override {
		return -ENOTSUP;
	}

	int mkdir(const char */*path*/, mode_t /*mode*/) override {
		return -ENOTSUP;
	}

	int rmdir(const char */*path*/) override {
		return -ENOTSUP;
	}

	// Unlink is ok

	int symlink(const char */*path*/, const char *) override {
		return -ENOTSUP;
	}

	int rename(const char */*sourcepath*/,
	           const char */*newpath*/,
	           const char */*sourcepath_raw*/,
	           const char */*newpath_raw*/) override {
		return -EPERM;
	}

	int chmod(const char */*path*/, mode_t /*mode*/) override {
		return -EPERM;
	}

	int chown(const char */*path*/, uid_t /*uid*/, gid_t /*gid*/) override {
		return -EPERM;
	}

	int truncate(const char *path, off_t newsize) override {
		if (newsize > 10000000) {
			return -EPERM;
		}
		return Module::truncate(path, newsize);
	}

	// open, read, write, statfs, flush, release, fsync
	// is ok - translatepath can only do authkeys anyways

	// opendir, releasedir, readdir is also tolerable

	int create(const char */*path*/, mode_t /*mode*/, struct fuse_file_info */*fi*/) override {
		return -EPERM;
	}
};

}
