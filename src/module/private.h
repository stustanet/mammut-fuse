#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {
 
class Private : public Module {
public:
	Private (std::shared_ptr<MammutConfig> config) :
		Module("private", config) {}

};

}
