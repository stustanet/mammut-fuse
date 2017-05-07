#include "options.h"

#include <iostream>
#include <memory>

namespace mammutfs {

enum optionIndex {
	UNKNOWN,
	HELP,
	CONFIGFILE,
	MOUNTPOINT,
	DONT_DEAMONIZE,
	VERBOSE
};

using A = option::Arg;

const option::Descriptor usage[] =
{
	{UNKNOWN, 0,"" , ""    ,A::None,		"USAGE: logger [options]\n\nOptions:" },
	{HELP,    0,"?" , "help",A::None,	"   --help, ?      Print usage and exit." },
	{DONT_DEAMONIZE,  0,"f", "foreground", A::None,	"--foreground, -f  Dont Deamonize." },
	{CONFIGFILE,  0,"c", "config", A::Optional,	"--config -c  Config file." },
	{VERBOSE,  0,"v", "", A::None,	"-v Be more verbose." },
	{0,0,0,0,0,0}
};



void Options::parse(int argc, char **argv) {
	//Assume "argv[0]" is the comand itself
	argc = argc - 1;
	argv = argv + 1;

	option::Stats stats(usage, argc, argv);

	auto options = std::make_unique<option::Option[]>(stats.options_max);
	auto buffer = std::make_unique<option::Option[]>(stats.buffer_max);

	option::Parser parse(usage, argc, argv, options.get(), buffer.get());

	if (parse.error()) {
		exit(1);
	}

	if (options[HELP] || argc <= 1) {
		int columns = getenv("COLUMNS")? atoi(getenv("COLUMNS")) : 80;
		option::printUsage(fwrite, stdout, usage, columns);
		exit(0);
	}

	if (options[DONT_DEAMONIZE]) {
		this->dont_deamonize = true;
	} else {
		this->dont_deamonize = false;
	}

	if (options[VERBOSE]) {
		this->verbose = true;
	} else {
		this->verbose = false;
	}

	if (options[CONFIGFILE]) {
		this->configfile = options[CONFIGFILE].arg;
	}

	for (int i = 0; i < parse.nonOptionsCount(); ++i) {
		switch (i) {
		case 0:
			break;
		case 1:
			mountpoint = parse.nonOption(i);
			break;
		}

		std::cout << "Non-option #" << i << ": " << parse.nonOption(i) << "\n";
	}
}

}
