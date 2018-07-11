#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {

class Backup : public Module {
public:
	Backup (const std::shared_ptr<MammutConfig> &config,
	        const std::shared_ptr<Communicator> &comm) :
		Module("backup", config, comm) {}
};

}
