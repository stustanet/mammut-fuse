#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {
 
class Private : public Module {
public:
	Private (const std::shared_ptr<MammutConfig> &config,
	         const std::shared_ptr<Communicator> &comm) :
		Module("private", config, comm) {}

};

}
