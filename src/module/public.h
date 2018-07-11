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

	virtual int mkdir(const char *path, mode_t mode) override {
		int ret = Module::mkdir(path, mode);
		inotify("MKDIR", path);
		return ret;
	}

	virtual int unlink(const char *path) override {
		int ret = Module::unlink(path);
		inotify("UNLINK", path);
		return ret;
	}

	virtual int rmdir(const char *path) override {
		int ret = Module::rmdir(path);
		inotify("RMDIR", path);
		return ret;
	}

	virtual int rename(const char *sourcepath,
	                   const char *newpath,
	                   const char *sourcepath_raw,
	                   const char *newpath_raw) override {
		std::cout << "from " << sourcepath_raw << " to " << newpath_raw << std::endl;
		int ret = Module::rename(sourcepath, newpath, sourcepath_raw, newpath_raw);
		this->comm->inotify("RENAME", sourcepath_raw, newpath_raw);
		return ret;
	}

	virtual int write(const char *path,
	                  const char *data,
	                  size_t size,
	                  off_t off,
	                  struct fuse_file_info *fi) override {
		int ret = Module::write(path, data, size, off, fi);
		if (ret == 0 && size > 0) {
			inotify("WRITE", path);
		}
		return ret;
	}

	virtual int truncate(const char *path, off_t off) override {
		int ret = Module::truncate(path, off);
		inotify("TRUNCATE", path);
		return ret;
	}

	virtual int release(const char *path,
	                    struct fuse_file_info *fi) override {
		int ret = Module::release(path, fi);
		inotify("RELEASE", path);
		return ret;
	}

	virtual int create(const char *path,
	                   mode_t mode,
	                   struct fuse_file_info *fi) override {
		int ret = Module::create(path, mode, fi);
		inotify("CREATE", path);
		return ret;
	}

protected:
	void inotify(const std::string &name, const std::string &path) {
		std::string translated;
		//this->translatepath(path, translated);
		this->comm->inotify(name, "public", path);
	}
private:
	std::shared_ptr<Communicator> comm;
};

}
