#pragma once

#include "../module.h"

#include "../mammut_config.h"
#include "../communicator.h"

namespace mammutfs {
 
class Public : public Module {
public:
	Public (std::shared_ptr<MammutConfig> config, std::shared_ptr<Communicator> comm) :
		Module("public", config),
		comm(comm) {}

private:
	std::shared_ptr<Communicator> comm;
};

}
