#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {
 
class Private : public Module {
public:
	Private (std::shared_ptr<MammutConfig> config) :
		Module(config) {}

	int translatepath(const std::string &path, std::string &out) {
		out = path;
		return 0;
	}

	std::string find_raid(const std::string &user, const std::string &path) {}
};

}
