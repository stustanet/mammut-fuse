#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {

class Backup : public Module {
public:
	Backup (std::shared_ptr<MammutConfig> config) :
		Module("backup", config) {}
};

}
