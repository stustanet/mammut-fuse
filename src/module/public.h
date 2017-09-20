#pragma once

#include "../module.h"

#include "../mammut_config.h"
#include "../communicator.h"

namespace mammutfs {

class Public : public Module {
public:
	Public (std::shared_ptr<MammutConfig> config, std::shared_ptr<Communicator> comm) :
		Module("public", config),
		comm(comm) {}

	virtual int mkdir(const char *path, mode_t mode) {
		int ret = Module::mkdir(path, mode);
		inotify("MKDIR", path);
		return ret;
	}

	virtual int unlink(const char *path) {
		int ret = Module::unlink(path);
		inotify("UNLINK", path);
		return ret;
	}

	virtual int rmdir(const char *path) {
		int ret = Module::rmdir(path);
		inotify("RMDIR", path);
		return ret;
	}

	virtual int rename(const char *a, const char *b) {
		int ret = Module::rename(a, b);
		std::string from = a;
		from += " -> ";
		from += b;
		inotify("RENAME", from);
		return ret;
	}

	virtual int truncate(const char *path, off_t off) {
		int ret = Module::truncate(path, off);
		inotify("TRUNCATE", path);
		return ret;
	}

	virtual int release(const char *path, struct fuse_file_info *fi) {
		int ret = Module::release(path, fi);
		inotify("RELEASE", path);
		return ret;
	}

	virtual int create(const char *path, mode_t mode, struct fuse_file_info *fi) {
		int ret = Module::create(path, mode, fi);
		inotify("CREATE", path);
		return ret;
	}

protected:
	void inotify(const std::string &name, const std::string &path) {
		std::string translated;
		this->translatepath(path, translated);
		this->comm->inotify(name, translated);
	}
private:
	std::shared_ptr<Communicator> comm;
};

}
