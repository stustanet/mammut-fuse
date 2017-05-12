#pragma once

#include "../libs/optionsparser.h"

#include <string>

namespace mammutfs {
class Options {
public:
	Options(int argc, char **argv) {
		parse(argc, argv);
	}

	void parse(int argc, char **argv);

	std::string configfile;
	std::string mountpoint;
	bool dont_deamonize;
	bool verbose;
};

}
