#pragma once

#include "../module.h"

#include "../mammut_config.h"

namespace mammutfs {
 
class PublicAnonLister : public Module {
public:
	PublicAnonLister (std::shared_ptr<MammutConfig> config) :
		Module("lister", config) {}

	
};

}
