#pragma once

#include "../module.h"
#include "../mammut_config.h"

#include <ctime>

namespace mammutfs {

class FullBackupList : public Module {
public:
	FullBackupList (const std::shared_ptr<MammutConfig> &config,
	        const std::shared_ptr<Communicator> &comm) :
		Module("all-backup-tree", config, comm) {
		comm->register_command(
			"BACKUPTREE_INVALIDATE",
			[this](const std::string &/*args*/, std::string &/*resp*/) {
				basepathmapping_creation = std::time_t(0);
				basepathmapping.clear();
				return true;
			});

		this->max_mapping_age = 300; // 300 Seconds
		this->basepathmapping_creation = 0;
	}

	int translatepath(const std::string &path, std::string &out) override {
		this->check_update_list();

		size_t pos = path.find('/', 1);
		std::string entry, remainder;
		if (pos == std::string::npos) {
			// The path is always starting with "/" - so we skip the first
			entry = path.substr(1);
			remainder = "";
		} else {
			entry = path.substr(1, pos - 1);
			remainder = path.substr(pos);
		}
		if (this->basepathmapping.empty()) {
			// This is the root directory, queried when issuing
			// statfs("/srv/backup") so we have to serve a valid directory here.
			out = this->config->get_first_raid();
			return 0;
		}

		auto it = this->basepathmapping.find(entry);
		if (it != this->basepathmapping.end()) {
			out = it->second + remainder;
			return 0;
		} else {
			return -ENOENT;
		}
		return -EPERM;
	}

	int opendir(const char *path, struct fuse_file_info *fi) override {
		if (strcmp(path, "/") == 0) {
			this->check_update_list();
			this->trace("fullbackuplist::opendir", path);

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
		if (strcmp(path, "/") == 0) {
			this->check_update_list();
			this->trace("backuptree::readdir", path);
			filler(buf, ".", NULL, 0);
			filler(buf, "..", NULL, 0);

			for (const auto  &entry : this->basepathmapping) {
				if (filler(buf, entry.first.c_str(), NULL, 0) != 0) {
					this->error("lister::readdir", "filler failed", path);
					return -ENOMEM;
				}
			}
			return 0;
		} else {
			return Module::readdir(path, buf, filler, offset, fi);
		}
		return 0;
	}

	int releasedir(const char *path, struct fuse_file_info *fi) {
		if (strcmp(path, "/") == 0) {
			this->check_update_list();
			this->trace("fullbackuplist::releasedir", path);

			return 0;
		} else {
			return Module::releasedir(path, fi);
		}
	}


private:
	// This contains the mapping userid -> raidpath
	// for example 001234 -> /srv/raids/kartoffel/backup/01234
	std::unordered_map<std::string, std::string> basepathmapping;
	std::time_t basepathmapping_creation;

	int max_mapping_age;

	void check_update_list(bool force=false) {
		std::time_t now = std::time(nullptr);
		if (!force
		    && this->basepathmapping_creation > now - this->max_mapping_age) {
			/*std::cout << "Skipping update" << std::endl
			          << "  force    " << force << std::endl
			          << "  created: " << this->basepathmapping_creation << std::endl
			          << "  max_age: " << this->max_mapping_age << std::endl
			          << "  sum:     " << now - this->max_mapping_age << std::endl
			          << "  now:     " << now << std::endl;*/
			return;
		}

		this->log(LOG_LEVEL::INFO, "scanning public dir listing");

		this->basepathmapping.clear();
		this->basepathmapping_creation = now;

		for (const auto &raid : config->raids) {
			std::string backupraid = raid + "/" + "backup";
			struct stat statbuf;
			memset(&statbuf, 0, sizeof(statbuf));
			int retval = ::stat(backupraid.c_str(), &statbuf);

			if (retval != 0 && errno == ENOENT) {
				//this->log(LOG_LEVEL::INFO, std::string("") + "raid " + backupraid + " does not contain a backup");
				continue;
			}

			// Iterate over the directory, adding in all directories (even empty ones)
			DIR *dfd;
			if ((dfd = ::opendir(backupraid.c_str())) == NULL) {
				this->log(LOG_LEVEL::ERR,
				          std::string() + "Can't open " + backupraid);
				continue;
			}

			struct dirent *dp;
			while ((dp = ::readdir(dfd)) != NULL) {
				std::string dirname = dp->d_name;

				if (dirname == "."
				    || dirname == "..") {
					continue;
				}

				std::string fqdirname = backupraid + "/" + dp->d_name;
				struct stat stbuf ;
				memset(&statbuf, 0, sizeof(statbuf));
				if (::stat(fqdirname.c_str(), &stbuf) == -1) {
					this->log(LOG_LEVEL::ERR,
					          std::string() + "Can't open subdir " + fqdirname);
					continue;
				}

				if ((stbuf.st_mode & S_IFMT ) == S_IFDIR ) {
					this->basepathmapping[dirname] = fqdirname;
				} else {
					this->log(LOG_LEVEL::ERR,
					          std::string() + "skipping non-directory " + fqdirname);
				}
			}
			::closedir(dfd);
		}
	}
};

}
