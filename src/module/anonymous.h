#pragma once

#include "../module.h"

#include "../mammut_config.h"
#include "../communicator.h"

namespace mammutfs {

class Anonymous : public Module {
public:
	Anonymous (std::shared_ptr<MammutConfig> config, std::shared_ptr<Communicator> comm) :
		Module("anon", config),
		comm(comm) {}

	virtual int mkdir(const char *path, mode_t mode) {
		Module::mkdir(path, mode);
		inotify("MKDIR", path);
	}

	virtual int unlink(const char *path) {
		Module::unlink(path);
		inotify("UNLINK", path);
	}

	virtual int rmdir(const char *path) {
		Module::rmdir(path);
		inotify("RMDIR", path);
	}

	virtual int rename(const char *a, const char *b) {
		Module::rename(a, b);
		std::string from = a;
		from += " -> ";
		from += b;
		inotify("RENAME", from);
	}

	virtual int truncate(const char *path, off_t off) {
		Module::truncate(path, off);
		inotify("TRUNCATE", path);
	}

	virtual int release(const char *path, struct fuse_file_info *fi) {
		Module::release(path, fi);
		inotify("RELEASE", path);
	}

	virtual int create(const char *path, mode_t mode, struct fuse_file_info *fi) {
		Module::create(path, mode, fi);
		inotify("CREATE", path);
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
