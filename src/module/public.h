#pragma once

#include "../module.h"

#include "../mammut_config.h"
#include "../communicator.h"

namespace mammutfs {

class Public : public Module {
public:
	Public (const std::shared_ptr<MammutConfig> &config,
	        const std::shared_ptr<Communicator> &comm) :
		Module("public", config, comm) {}

	virtual int mkdir(const char *path, mode_t mode) override {
		mode |= S_IROTH | S_IXOTH;
		int ret = Module::mkdir(path, mode);
		if (ret == 0)
			inotify("MKDIR", path);
		return ret;
	}

	virtual int unlink(const char *path) override {
		int ret = Module::unlink(path);
		if (ret == 0)
			inotify("UNLINK", path);
		return ret;
	}

	virtual int rmdir(const char *path) override {
		int ret = Module::rmdir(path);
		if (ret == 0)
			inotify("RMDIR", path);
		return ret;
	}

	virtual int rename(const char *sourcepath,
	                   const char *newpath,
	                   const char *sourcepath_raw,
	                   const char *newpath_raw) override {
		std::cout << "from " << sourcepath_raw << " to " << newpath_raw << std::endl;
		int ret = Module::rename(sourcepath, newpath, sourcepath_raw, newpath_raw);
		if (ret == 0)
			this->comm->inotify("RENAME", sourcepath_raw, newpath_raw);
		return ret;
	}

	virtual int write(const char *path,
	                  const char *data,
	                  size_t size,
	                  off_t off,
	                  struct fuse_file_info *fi) override {
		int ret = Module::write(path, data, size, off, fi);
#ifdef ENABLE_WRITE_NOTIFY
		if (ret == 0 && size > 0) {
			inotify("WRITE", path);
		}
#endif
		return ret;
	}

	virtual int truncate(const char *path, off_t off) override {
		int ret = Module::truncate(path, off);
		if (ret == 0)
			inotify("TRUNCATE", path);
		return ret;
	}

	virtual int release(const char *path,
	                    struct fuse_file_info *fi) override {

		int retstat = 0;
		std::string translated;
		if ((retstat = this->translatepath(path, translated))) {
			this->info("stat", "translatepath failed", path);
			return retstat;
		}
		bool changed = false;
		if (this->file(translated, fi).changed()) {
			changed = true;
		}

		int ret = Module::release(path, fi);
		if (ret == 0) {
			if (changed)
				inotify("CHANGED", path);
		}
		return ret;
	}

	virtual int create(const char *path,
	                   mode_t mode,
	                   struct fuse_file_info *fi) override {
		mode |= S_IROTH;
		int ret = Module::create(path, mode, fi);
		inotify("CREATE", path);
		return ret;
	}

	virtual int chmod(const char *path, mode_t mode) override {
		if (S_ISREG(mode)) {
			mode |= S_IROTH;
		} else if (S_ISDIR(mode)) {
			mode |= S_IROTH | S_IXOTH;
		}
		return Module::chmod(path, mode);
	}

protected:
	void inotify(const std::string &name, const std::string &path) {
		this->comm->inotify(name, "public", path);
	}
};

}
