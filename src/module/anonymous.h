#pragma once

#include "../module.h"

#include "../mammut_config.h"
#include "../communicator.h"

#include <algorithm>

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

	virtual bool is_path_valid(const std::string &path) override {
		// Do not show any "./mammut-suffix" files.
		// They are used to store the _ABC suffix in the anonmap
		size_t delimiter = path.find_last_of('/');
		if (delimiter != std::string::npos
		    && 0 == path.compare(delimiter, path.size(), ANON_SUFFIX_FILENAME)) {
			return false;
		}
		return true;
	}

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
		// remove the suffix file whenever a anon root directory should be removed.
		{
			std::string spath = path;
			auto path_len = std::count_if(spath.begin(), spath.end(), [](char c){ return c == '/'; });
			if (path_len == 1) {
				// we need to construct the path to the suffix file manually,
				// since any this->translatepath access to the suffix file is disabled.
				std::string translated;
				int retstat = 0;
				if ((retstat = this->translatepath(path, translated))) {
					this->info("rmdir", "root translatepath failed", path);
					return retstat;
				}
				translated += ANON_SUFFIX_FILENAME;
				retstat = ::unlink(translated.c_str());
				if(retstat != 0) {
					this->error(errno, "rmdir", "suffixfile", translated);
				}
			}
		}

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
		}
		return ret;
	}

	virtual int create(const char *path,
	                   mode_t mode,
	                   struct fuse_file_info *fi) override {
		mode |= S_IROTH;
		int ret = Module::create(path, mode, fi);
		if (ret == 0)
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
	void inotify(const std::string &operation, const std::string &path) {
		this->comm->inotify(operation, "anonym", path);
	}

private:
	const std::map<std::string, std::string> *anon_map;
};

}
