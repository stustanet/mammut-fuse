#pragma once

#include "module.h"

#include "mammut_config.h"
#include "communicator.h"

namespace mammutfs {

class FileModule : public Module {
public:
	FileModule (const std::string &name,
	            const std::shared_ptr<MammutConfig> &config,
	            const std::shared_ptr<Communicator> &comm) :
		Module(name, config, comm) {
	}

	int translatepath(const std::string &path, std::string &out) override {
		if (path == "/") {
			int rc = Module::translatepath(path, out);
			if (rc == 0) {
				// remove trailing "/"
				out.pop_back();
			}
			return rc;
		} else {
			return -ENOENT;
		}
	}

	/**
	 * Changed notifier will be called whenever the corresponding file has been
	 * changed after release()-ing the file. It will also be called on startup.
	 **/
	virtual void changed() = 0;

	/**
	 * Called when the file needs to be truncated or removed or initially created
	 * in other words filled with default content
	 **/
	virtual void make_default() = 0;


	int getattr(const char *path, struct stat *statbuf) override {
#ifdef TRACE_GETATTR
		this->trace("filemodule::getattr", path);
#endif
		if (strcmp(path, "/") == 0) {
			std::string out;
			int retstat = this->translatepath(path, out);
			if (retstat != 0) return retstat;

			retstat = ::lstat(out.c_str(), statbuf);
			if (retstat != 0) return -errno;

			return 0;
		} else {
			this->trace("filemodule::getattr: FAILED access to non-root path!", path);
			return -ENOENT;
		}
	}

	// getattr is also protected by translatepath
	int readlink(const char *path, char */*link*/, size_t /*size*/) override {
		this->trace("filemod::readlink", path);
		return -EPERM;
	}

	int mkdir(const char *path, mode_t /*mode*/) override {
		this->trace("filemod::mkdir", path);
		return -EPERM;
	}

	int rmdir(const char *path) override {
		this->trace("filemod::rmdir", path);
		return -EPERM;
	}

	int symlink(const char *path, const char *) override {
		this->trace("filemod::symlink", path);
		return -ENOTSUP;
	}

	int rename(const char *sourcepath,
	           const char */*newpath*/,
	           const char */*sourcepath_raw*/,
	           const char */*newpath_raw*/) override {
		this->trace("filemod::rename", sourcepath);
		return -EPERM;
	}

	int chmod(const char *path, mode_t /*mode*/) override {
		this->trace("filemod::chmod", path);
		return -EPERM;
	}

	int chown(const char *path, uid_t /*uid*/, gid_t /*gid*/) override {
		this->trace("filemod::chown", path);
		return -EPERM;
	}

	// open, read, write, statfs, flush, release, fsync
	// opendir, releasedir, readdir is also tolerable

	int create(const char *path,
	           mode_t /*mode*/,
	           struct fuse_file_info */*fi*/) override {
		this->trace("filemod::create", path);
		return -EPERM;
	}

	int unlink(const char *path) override {
		this->trace("filemod::unlinks", path);
		return -EPERM;
	}

	int write(const char *path,
	          const char *buf,
	          size_t size,
	          off_t offset,
	          struct fuse_file_info *fi) override {
		this->has_changed = true;
		return Module::write(path, buf, size, offset, fi);
	}

	int release(const char *path, struct fuse_file_info *fi) {
		this->trace("filemod::release", path);
		int rc = Module::release(path, fi);

		std::string out;
		this->translatepath(path, out);

		struct stat statbuf;
		::stat(out.c_str(), &statbuf);

		if (statbuf.st_size == 0) {
			this->info("file was empty, will create new one", out, "");
			this->make_default();
			this->has_changed = false;
		}

		if (this->has_changed) {
			this->changed();
		}
		return rc;
	}

protected:
	bool has_changed;
};
}
