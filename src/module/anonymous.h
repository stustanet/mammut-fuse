#pragma once

#include "../module.h"

#include "../mammut_config.h"
#include "../communicator.h"

#include <algorithm>

namespace mammutfs {

class Anonymous : public Module {
public:
	Anonymous (std::shared_ptr<MammutConfig> config, std::shared_ptr<Communicator> comm, const std::map<std::string, std::string> *anon_map) :
		Module("anonym", config),
		comm(comm),
		anon_map(anon_map) {}

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
		bool changed = false;
		{
			// The path is invalid after a release - so better not safe any
			// reference to it.
			std::string translated;
			this->translatepath(path, translated);
			auto f = file(translated);
			changed = f.changed();
		}
		int i = Module::release(path, fi);
		inotify("RELEASE", path);
		if (changed) {
			inotify("CHANGED", path);
		}
		return i;
	}

	virtual int create(const char *path, mode_t mode, struct fuse_file_info *fi) {
		int i = Module::create(path, mode, fi);
		inotify("CREATE", path);
		return i;
	}

protected:
	void inotify(const std::string &name, const std::string &path) {
		// Reverse translate the anon path by clever reverse searching within the anon mapping
		// The input path is the relative path within this module, i.e. if the path was
		// `mnt/anonym/test/x/y/z` the $path variable is `/test/x/y/z`.
		// What we do is strip it down to the first path (`test`), create a partial anon mapping
		// `a_test_` and search that with a partial-matching in the anon mapping.

		size_t split = path.find('/', 1);
		std::string subpath;
		std::string toplevel;
		if (split == std::string::npos) { // no / found => the full path is our root!
			toplevel = path;
		} else {
			// Offset of 1 because it starts with /
			toplevel = path.substr(1, split-1);
			subpath = path.substr(split+1);
		}
		std::string anon_path = std::string("a_") + toplevel + "_";
		auto it = std::find_if(this->anon_map->begin(), this->anon_map->end(),
		                       [&anon_path](const std::pair<std::string, std::string> &p) {
				// The Anon mapping _XXX suffix is exactly 3 chars long
				if (p.first.length() == anon_path.length() + 3) {
					// Compare the prefixes
					if (p.first.compare(0, anon_path.length(), anon_path)) {
						return true;
					}
				}
				return false;
			});
		if (it == this->anon_map->end()) {
			this->warn("inotify", "could not reverse translate path", path);
			return;
		}
		// `it` now points to the pair anon:path

		std::stringstream ss;
		ss << it->first << "/" << subpath;
		this->log(LOG_LEVEL::INFO, name, ss.str());
		this->comm->inotify(name, ss.str());
	}
private:
	std::shared_ptr<Communicator> comm;
	const std::map<std::string, std::string> *anon_map;
};

}
