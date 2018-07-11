#pragma once

#include "../module.h"

#include "../mammut_config.h"
#include "../communicator.h"

namespace mammutfs {


/** FIXME: There is a huge problem within mammutfs.
 * Whenever a new anonymous root folder is created, it has to have an
 * assigned random suffix.
 * Since we do not want to distribute the non-anonymous filename (in /srv/raids/.../anonym/user/file,
 * but the anon file (/srv/public/a_file_XXX), the _XXX suffix is needed.
 * This is currently assigned from the mammutfsd - not from us. So we actually cannot distribute this
 * information.
 * WE HAVE TO REDESIGN THE MAMMUTFSD <--> MAMMUTFS COMMUNICATION!!
 **/
class Anonymous : public Module {
public:
	Anonymous (std::shared_ptr<MammutConfig> config,
	           std::shared_ptr<Communicator> comm,
	           const std::map<std::string, std::string> *anon_map) :
		Module("anonym", config),
		comm(comm),
		anon_map(anon_map) {
	}

	virtual int mkdir(const char *path, mode_t mode) override {
		int i = Module::mkdir(path, mode);
		inotify("MKDIR", path);
		return i;
	}

	virtual int unlink(const char *path) override {
		int i = Module::unlink(path);
		inotify("UNLINK", path);
		return i;
	}

	virtual int rmdir(const char *path) override {
		int i = Module::rmdir(path);
		inotify("RMDIR", path);
		return i;
	}

	virtual int rename(const char *sourcepath,
	                   const char *newpath,
	                   const char *sourcepath_raw,
	                   const char *newpath_raw) override {
		int i = Module::rename(sourcepath, newpath, sourcepath_raw, newpath_raw);
		this->comm->inotify("RENAME", sourcepath_raw, newpath_raw);
		return i;
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
		int i = Module::truncate(path, off);
		inotify("TRUNCATE", path);
		return i;
	}

	virtual int release(const char *path, struct fuse_file_info *fi) override {
		int i = Module::release(path, fi);
		inotify("RELEASE", path);
		return i;
	}

	virtual int create(const char *path,
	                   mode_t mode,
	                   struct fuse_file_info *fi) override {
		int i = Module::create(path, mode, fi);
		inotify("CREATE", path);
		return i;
	}

protected:
	void inotify(const std::string &operation, const std::string &path) {
		this->comm->inotify(operation, "anonym", path);
	}

private:
	std::shared_ptr<Communicator> comm;
	const std::map<std::string, std::string> *anon_map;
};

}
