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

	auto resolver = std::make_shared<mammutfs::ModuleResolver>();
	auto config = std::make_shared<mammutfs::MammutConfig>(configfile,
	                                                       argc,
	                                                       argv,
	                                                       resolver);
	auto communicator = std::make_shared<mammutfs::Communicator>(config);

	/// Add all Modules to the following list
	resolver->registerModule("default", std::make_shared<mammutfs::Default>(config));
	resolver->registerModule("private", std::make_shared<mammutfs::Private>(config));
	resolver->registerModule("public", std::make_shared<mammutfs::Public>(config, communicator));
	resolver->registerModule("anonymous", std::make_shared<mammutfs::Anonymous>(config, communicator));
	resolver->registerModule("backup", std::make_shared<mammutfs::Backup>(config));
	resolver->registerModule("lister", std::make_shared<mammutfs::PublicAnonLister>(config, communicator));

	config->filterModules(resolver);

	mammutfs::mammut_main(resolver, config);
}
