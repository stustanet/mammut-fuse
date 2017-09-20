#include "resolver.h"

#include "mammut_fuse.h"
#include "mammut_config.h"

#include "module/default.h"
#include "module/private.h"
#include "module/public.h"
#include "module/anonymous.h"
#include "module/backup.h"
#include "module/lister.h"

int main(int argc, char **argv) {
	const char *configfile = "mammutfs.cfg";
	if (argc < 2) {
		std::cout << "You did not specify a config file (as first argument), \n"
		          << "using default one at " << configfile << std::endl;
	} else {
		configfile = argv[1];
		std::cout << "Configfile specified. Using " << configfile << std::endl;
		argv = &argv[1]; // skip over the first argument
		argc--;
	}

	// Setting up the resolver, that manages active modules
	auto resolver = std::make_shared<mammutfs::ModuleResolver>();
	// Setting up the config that manges program wide configuration
	auto config = std::make_shared<mammutfs::MammutConfig>(configfile,
	                                                       argc,
	                                                       argv,
	                                                       resolver);
	// Setting up the communicator, that manges the unix socket
	auto communicator = std::make_shared<mammutfs::Communicator>(config);

	/// Add all Modules to the following list
	resolver->registerModule("default", std::make_shared<mammutfs::Default>(config));
	resolver->registerModule("private", std::make_shared<mammutfs::Private>(config));
	resolver->registerModule("public", std::make_shared<mammutfs::Public>(config, communicator));
	resolver->registerModule("anonym", std::make_shared<mammutfs::Anonymous>(config, communicator));
	resolver->registerModule("backup", std::make_shared<mammutfs::Backup>(config));
	resolver->registerModule("lister", std::make_shared<mammutfs::PublicAnonLister>(config, communicator));

	// Filter the modules to the active ones
	config->filterModules(resolver);

	// Hit the road
	mammutfs::mammut_main(resolver, config);
}
