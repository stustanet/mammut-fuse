#include "resolver.h"

#include "mammut_fuse.h"
#include "mammut_config.h"

#include "module/default.h"
#include "module/private.h"
#include "module/public.h"
#include "module/anonymous.h"
#include "module/backup.h"
#include "module/lister.h"

#include <sstream>
#include <memory>

#include <syslog.h>

static std::shared_ptr<mammutfs::ModuleResolver> resolver;
static std::shared_ptr<mammutfs::MammutConfig> config;
static std::shared_ptr<mammutfs::Communicator> communicator;

int main(int argc, char **argv) {
	openlog("mammutfs", LOG_PID, 0);

	const char *configfile = "mammutfs.cfg";
	if (argc >= 2) {
		configfile = argv[1];
		argv = &argv[1]; // skip over the first argument
		argc--;
	}
	std::cout << "using config: " << configfile << std::endl;

	// Setting up the resolver, that manages active modules
	resolver = std::make_shared<mammutfs::ModuleResolver>();

	// Setting up the config that manges program wide configuration
	config = std::make_shared<mammutfs::MammutConfig>(
		configfile,
		argc,
		argv,
		resolver);

	// Hit the road
	// This will fork, and afterwards call setup_main
	mammutfs::mammut_main(resolver, config);
}

void setup_main() {
	// We start this in the context of fuse - hopefully the sockets & threads survive
	// Setting up the communicator, that manges the unix socket
	communicator = std::make_shared<mammutfs::Communicator>(config);

	// Start the communicator threads
	communicator->start();

	// We need the anon lister first, because it will generate the anon mapping
	auto anon_lister = std::make_shared<mammutfs::PublicAnonLister>(config, communicator);
	/// Add all Modules to the following list
	resolver->registerModule("default",
	                         std::make_shared<mammutfs::Default>(config, communicator));
	resolver->registerModule("lister", anon_lister);
	resolver->registerModule("private",
	                         std::make_shared<mammutfs::Private>(config, communicator));
	resolver->registerModule("public",
	                         std::make_shared<mammutfs::Public>(config, communicator));
	resolver->registerModule(
		"anonym",
		std::make_shared<mammutfs::Anonymous>(config,
		                                      communicator,
		                                      anon_lister->get_mapping()));
	resolver->registerModule("backup",
	                         std::make_shared<mammutfs::Backup>(config, communicator));

	// Filter the modules to the active ones
	config->filterModules(resolver);


	std::stringstream ss;
	ss << "New Mammutfs for user " << config->username()
	   << " at " << config->mountpoint();
	syslog(LOG_INFO, ss.str().c_str());

	std::cerr << ss.str() << std::endl;
}
