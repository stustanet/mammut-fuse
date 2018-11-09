#pragma once

#include "../module.h"

#include "../mammut_config.h"
#include "../communicator.h"

namespace mammutfs {


/**
 * Whenever a new anonymous root folder is created, it has to have an
 * assigned random suffix.
 * Since we do not want to distribute the non-anonymous filename (in
 * /srv/raids/.../anonym/user/file), but the anon file (/srv/public/a_file_XXX),
 * the _XXX suffix is needed. This is currently assigned from the mammutfsd - not
 * from us. So we actually cannot distribute this information.
 * Therefore we send notify events with our path, and the mammutfsd has to do the
 * bookkeeping which file is which - and notify us, when we have to reload the anonmap
 **/
class Anonymous : public Module {
public:
	Anonymous (const std::shared_ptr<MammutConfig> &config,
	           const std::shared_ptr<Communicator> &comm,
	           const std::map<std::string, std::string> *anon_map) :
		Module("anonym", config, comm),
		anon_map(anon_map) {
	}

	virtual int mkdir(const char *path, mode_t mode) override {
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
		if (ret > 0 && size > 0) {
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

	virtual int release(const char *path, struct fuse_file_info *fi) override {
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
				// if it has changed, we also trigger a special inotify!
				inotify("CHANGED", path);
			// We will always issue a release!
			inotify("RELEASE", path);
		}
		return ret;
	}

	virtual int create(const char *path,
	                   mode_t mode,
	                   struct fuse_file_info *fi) override {
		int ret = Module::create(path, mode, fi);
		if (ret == 0)
			inotify("CREATE", path);
		return ret;
	}

protected:
	void inotify(const std::string &operation, const std::string &path) {
		this->comm->inotify(operation, "anonym", path);
	}

private:
	const std::map<std::string, std::string> *anon_map;
};

}
