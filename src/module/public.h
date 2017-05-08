#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {
 
class Public : public Module {
public:
	Public (std::shared_ptr<MammutConfig> config) :
		Module("public", config) {}
};

}
