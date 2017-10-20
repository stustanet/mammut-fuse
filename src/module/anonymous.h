#pragma once

#include "../module.h"

#include "../mammut_config.h"
#include "../communicator.h"

namespace mammutfs {

class Anonymous : public Module {
public:
	Anonymous (std::shared_ptr<MammutConfig> config, std::shared_ptr<Communicator> comm) :
		Module("anonym", config),
		comm(comm) {}

	virtual int mkdir(const char *path, mode_t mode) {
		int i = Module::mkdir(path, mode);
		inotify("MKDIR", path);
		return i;
	}

	virtual int unlink(const char *path) {
		int i = Module::unlink(path);
		inotify("UNLINK", path);
		return i;
	}

	virtual int rmdir(const char *path) {
		int i = Module::rmdir(path);
		inotify("RMDIR", path);
		return i;
	}

	virtual int rename(const char *sourcepath,
	                   const char *newpath,
	                   const char *sourcepath_raw,
	                   const char *newpath_raw) {
		int i = Module::rename(sourcepath, newpath, sourcepath_raw, newpath_raw);
		this->comm->inotify("RENAME", sourcepath_raw, newpath_raw);
		return i;
	}

	virtual int truncate(const char *path, off_t off) {
		int i = Module::truncate(path, off);
		inotify("TRUNCATE", path);
		return i;
	}

	virtual int release(const char *path, struct fuse_file_info *fi) {
		int i = Module::release(path, fi);
		inotify("RELEASE", path);
		return i;
	}

	virtual int create(const char *path, mode_t mode, struct fuse_file_info *fi) {
		int i = Module::create(path, mode, fi);
		inotify("CREATE", path);
		return i;
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
