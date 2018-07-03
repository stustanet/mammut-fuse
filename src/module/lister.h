#pragma once

#include "../module.h"
#include "../mammut_config.h"

#include <fstream>

namespace mammutfs {

class PublicAnonLister : public Module {
public:
	PublicAnonLister (std::shared_ptr<MammutConfig> config, std::shared_ptr<Communicator> comm) :
		Module("lister", config),
		comm(comm) {
		comm->register_command("CLEARCACHE", [this](const std::string &) {
				list.clear();
				return true;
			});
		comm->register_command("FORCE-RELOAD", [this](const std::string &) {
				return rescan();
			});

		// When the mapping file changes, rescan the file
		config->register_changeable("anon_mapping_file", [this]() {
				rescan();
			});

		rescan();
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

		if (list.empty()) {
			rescan();
		}

		size_t pos = path.find('/', 1);
		std::string entry = path.substr(1, pos - 1);

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
		// TODO: Maybe we want to keep UIDs for public listing, this way we will eliminate all of them
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
			return Module::opendir(path, fi);
		}
	}

	int readdir(const char *path,
	                   void *buf,
	                   fuse_fill_dir_t filler,
	                   off_t offset,
	                   struct fuse_file_info *fi) override {
		//todo load shared listing
		if (strcmp(path, "/") == 0) {
			this->trace("lister::readdir", path);
			filler(buf, ".", NULL, 0);
			filler(buf, "..", NULL, 0);
			filler(buf, "core", NULL, 0);
			for (const auto  &entry : list) {
				if (filler(buf, entry.first.c_str(), NULL, 0) != 0) {
					return -ENOMEM;
				}
			}
		} else {
			return Module::readdir(path, buf, filler, offset, fi);
		}
		return 0;
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
	bool rescan() {
		// Read the mapping file
		std::string anon_mapping_file = config->anon_mapping_file();
		this->info("scan", "using anon map", anon_mapping_file);
		std::ifstream file(anon_mapping_file, std::ios::in);
		if (!file) {
			this->warn("scan", "error opening annon mapping` file ", anon_mapping_file);
			return false;
		}
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
			list.insert(p);
		}
		std::stringstream ss;
		ss << "found " << list.size() << " elements.";
		this->info("scan", ss.str(), "");
		return true;
	}


	std::map<std::string, std::string> list;
	std::shared_ptr<Communicator> comm;
};

}
