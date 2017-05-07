#pragma once

#include "resolver.h"

#include "mammut_config.h"

namespace mammutfs {

void mammut_main (std::shared_ptr<ModuleResolver> resovler,
                  std::shared_ptr<MammutConfig> config);

}
