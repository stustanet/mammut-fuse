#pragma once

#include "../filemodule.h"
#include "../mammut_config.h"

#include <fstream>

#include <errno.h>
#include <sys/stat.h>

namespace mammutfs {

class Authkeys : public FileModule {
public:
	Authkeys(const std::shared_ptr<MammutConfig> &config,
	         const std::shared_ptr<Communicator> &comm) :
		FileModule("authkeys", config, comm) {
	}

	// This needs to be hacked, because the authorized keys are located at
	// .../authkeys/<UID>/authorized_keys
	int find_raid(std::string &path) override {
		int rc = Module::find_raid(path);

		if (rc == 0) {
			path += "/authorized_keys";
		}
		return rc;
	}

	void changed() override {
		// Do something?
		this->has_changed = false;
	}

	void make_default() override {
		std::string out;
		this->translatepath("/", out);

		std::ifstream file(out, std::ios::in);
	}
};

}
