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

		rescan();
	}

	int translatepath(const std::string &path, std::string &out) override {
		if (path == "/") {
			out = ".";
			return 0;
		}
		if (list.empty()) {
			rescan();
		}

		int pos = path.find('/', 1);
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



	int getattr(const char *path, struct stat *statbuf) {
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

private:
	bool rescan() {
		// Read the mapping file
		std::cout << "reading file: " << config->anon_mapping_file << std::endl;
		std::ifstream file(config->anon_mapping_file, std::ios::in);
		if (!file) {
			std::cout << "Error opening anon mapping file" << std::endl;
			return false;
		}
		std::string line;
		while(std::getline(file, line, '\n')) {
			size_t split = line.find(':');
			if (split == std::string::npos) {
				std::cout << "Skipping invalid line: " << line;
				continue;
			}
			auto p = std::make_pair(
				      		line.substr(0, split),
				            line.substr(split+1));
			list.insert(p);
		//	std::cout << "ANON: " << p.first << " ----> " << p.second << std::endl;
		}
		std::cout << "found " << list.size() << " elements." << std::endl;
	}

	std::map<std::string, std::string> list;

	std::shared_ptr<Communicator> comm;
};

}
