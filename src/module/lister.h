#pragma once

#include "../module.h"
#include "../mammut_config.h"

#include <fstream>

namespace mammutfs {

class PublicAnonLister : public Module {
public:
	PublicAnonLister (const std::shared_ptr<MammutConfig> &config,
	                  const std::shared_ptr<Communicator> &comm) :
		Module("lister", config, comm){
		comm->register_command(
			"CLEARCACHE",
			[this](const std::string &, std::string &/*resp*/) {
				list.clear();
				return true;
			}, "Clear the anonmap");
		comm->register_command(
			"FORCE-RELOAD",
			[this](const std::string &, std::string &resp) {
				int cnt = rescan();
				if (cnt < 0) return false;
				std::stringstream ss;
				ss << "{\"newsize\":\"" << cnt << "\"}";
				resp = ss.str();
				return true;
			}, "Reload the anonmap");

		// When the mapping file changes, rescan the file
		config->register_changeable("anon_mapping_file", [this]() {
				rescan();
			});

		// Rescan on creation seems to trigger deadlocks during the `open`ing of
		// the anonmap - so we should not do that, but scan on demand, whenever
		// the first real access has happened.
		// rescan();
	}

	/**
	 * Get mapping from anon name to real path name
	 */
	const std::map<std::string, std::string> *get_mapping() const {
		return &this->list;
	}

	int translatepath(const std::string &path, std::string &out) override {
		if (path == "/") {
			out = "";
			return 0;
		}
		if (path == "/core") {
			out = "core";
			return -ENOTSUP;
		}

		size_t pos = path.find('/', 1);
		std::string entry = path.substr(1, pos - 1);

		// TODO: is this possible - we will aggressively scan the anonmap here
		// if it was not yet opened.
		if (this->list.empty()) {
			this->rescan();
		}
		auto it = list.find(entry);
		if (it != list.end()) {
			if (pos == std::string::npos) {
				out = it->second;
			} else {
				out = it->second + "/" + path.substr(pos + 1);
			}
		} else {
			return -ENOENT;
		}

		return 0;
	}


	virtual int getattr(const char *path, struct stat *statbuf) override {
		memset(statbuf, 0, sizeof(*statbuf));
		if (strcmp(path, "/core") == 0) {
			statbuf->st_dev         = 0;               // IGNORED Device
			statbuf->st_ino         = 998;             // IGNORED inode number
			//statbuf->st_mode        = S_IFDIR | 0755;  // Protection
			statbuf->st_mode        = S_IFREG | 0555;  // Protection
			statbuf->st_nlink       = 0;               // Number of Hard links
			statbuf->st_uid         = config->anon_uid;
			statbuf->st_gid         = config->anon_gid;
			statbuf->st_rdev        = 0;
			statbuf->st_size        = 1ull << 62;
			statbuf->st_blksize     = 0;  // IGNORED
			statbuf->st_blocks      = 0;
			statbuf->st_atim.tv_sec = 0;  // Last Access
			statbuf->st_mtim.tv_sec = 0;  // Last Modification
			statbuf->st_ctim.tv_sec = 0;  // Last Status change
			return 0;
		}

		int retstat = Module::getattr(path, statbuf);
		// Eliminate all User-IDs from the items
		// TODO: Maybe we want to keep UIDs for public listing, this way we will
		// eliminate all of them
		statbuf->st_uid = config->anon_uid;
		statbuf->st_gid = config->anon_gid;
		return retstat;
	}


	int mkdir(const char *path, mode_t mode) override {
		if (strcmp(path, "/") == 0) {
			this->trace("lister::mkdir", path);
			return -EPERM;
		} else {
			// Check if need to add to lister.

			mode = ((mode & 0770) | 0005);
			return Module::mkdir(path, mode); // Maybe test, if this is allowed and
			// return -EPERM early.
		}
	}

	int opendir(const char *path, struct fuse_file_info *fi) override {
		if (strcmp(path, "/") == 0) {
			this->trace("lister::opendir", path);
			return 0;
		} else {
			int retval = Module::opendir(path, fi);
			// If the action did not succeed because the path was not found -
			// maybe we need to use this as a trigger to rescan, and optimistically
			// retry opening
			if (retval == -ENOENT) {
				if (try_rescan() != 0) {
					retval = Module::opendir(path, fi);
				}
			}
			return retval;
		}
	}

	int readdir(const char *path,
	                   void *buf,
	                   fuse_fill_dir_t filler,
	                   off_t offset,
	                   struct fuse_file_info *fi) override {
		if (strcmp(path, "/") == 0) {
			if (this->list.empty()) {
				this->rescan();
			}

			this->trace("lister::readdir", path);
			filler(buf, ".", NULL, 0);
			filler(buf, "..", NULL, 0);
			filler(buf, "core", NULL, 0);

			for (const auto  &entry : list) {
				if (filler(buf, entry.first.c_str(), NULL, 0) != 0) {
					this->error("lister::readdir", "filler failed", path);
					return -ENOMEM;
				}
			}
			return 0;
		}

		int retval = Module::readdir(path, buf, filler, offset, fi);
		// If the action did not succeed because the path was not found -
		// maybe we need to use this as a trigger to rescan, and optimistically
		// retry opening
		if (retval == -ENOENT) {
			if (try_rescan() != 0) {
				retval = Module::readdir(path, buf, filler, offset, fi);
			}
		}
		return retval;
	}

	virtual int open(const char *path, struct fuse_file_info *fi) override {
		if (strcmp("/core", path) == 0) {
			return 0;
		} else {
			return Module::open(path, fi);
		}
	}

	virtual int read(const char *path, char *buf, size_t size, off_t offset,
			struct fuse_file_info *fi) override {
		if (strcmp("/core", path) == 0) {
			memset(buf, 42, size);
			return size;
		} else {
			return Module::read(path, buf, size, offset, fi);
		}
	}

private:
	int rescan() {
		// Read the mapping file
		std::string anon_mapping_file = config->anon_mapping_file();
		std::stringstream ss;
		ss << "using anon map: " << anon_mapping_file;

		// Save timestamp of the current anonmap
		struct stat statbuf;
		::stat(anon_mapping_file.c_str(), &statbuf);
		this->anonmap_mtime = statbuf.st_mtim;

		std::ifstream file(anon_mapping_file, std::ios::in);
		if (!file) {
			this->warn("scan", "error opening annon mapping` file ", anon_mapping_file);
			return -1;
		}
		this->list.clear();
		std::string line;
		while(std::getline(file, line, '\n')) {
			size_t split = line.find(':');
			if (split == std::string::npos) {
				this->info("scan", "Skipping invalid line: ", line);
				continue;
			}
			auto p = std::make_pair(
				line.substr(0, split),
				line.substr(split+1));
			this->list.insert(p);
		}
		ss << "; found " << this->list.size() << " elements.";
		this->info("scan", ss.str(), "");
		return this->list.size();
	}

	int try_rescan() {
		// Rescan the anonmap, if it was changed since the last read
		//(for example if we encounter a ENOENT at root level path)

		std::string anon_mapping_file = config->anon_mapping_file();
		struct stat statbuf;
		::stat(anon_mapping_file.c_str(), &statbuf);
		if (statbuf.st_mtim.tv_sec != this->anonmap_mtime.tv_sec
		    || statbuf.st_mtim.tv_nsec != this->anonmap_mtime.tv_nsec) {
			return rescan();
		} else {
			return 0;
		}
	}

	timespec anonmap_mtime;
	std::map<std::string, std::string> list;
	std::shared_ptr<Communicator> comm;
};

}
