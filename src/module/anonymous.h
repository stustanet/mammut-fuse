#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {

class Anonymous : public Module {
public:
	Anonymous (std::shared_ptr<MammutConfig> config, std::shared_ptr<Communicator> comm) :
		Module("anonymous", config),
		comm(comm) {}

private:
	std::shared_ptr<Communicator> comm;
};

}
