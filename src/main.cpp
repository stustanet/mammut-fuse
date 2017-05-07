#include "resolver.h"

#include "mammut_fuse.h"
#include "mammut_config.h"

#include "module/default.h"
#include "module/private.h"

int main(int argc, char **argv) {
	const char *configfile = "mammutfs.cfg";

	auto resolver = std::make_shared<mammutfs::ModuleResolver>();
	auto config = std::make_shared<mammutfs::MammutConfig>(configfile,
	                                                       argc,
	                                                       argv,
	                                                       resolver);
	

	/// Add all Modules to the following list
	resolver->registerModule("default", std::make_shared<mammutfs::Default>(config));
	resolver->registerModule("private", std::make_shared<mammutfs::Private>(config));

	config->filterModules(resolver);

	mammutfs::mammut_main(resolver, config);
}
