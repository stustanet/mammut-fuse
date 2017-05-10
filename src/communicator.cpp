#include "communicator.h"

#include "mammut_config.h"

#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>

#include <algorithm>
#include <string>

namespace mammutfs {

Communicator::Communicator (std::shared_ptr<MammutConfig> config) :
	config(config) {
	std::string socketname;
	config->lookupValue("socket_directory", socketname);
	socketname += "/" + config->username;

	std::cout << "Using socket " << socketname << std::endl;

	unlink(socketname.c_str());

	connect_socket = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (connect_socket < 0) {
		perror("socket");
		exit(-1);
	}

	struct sockaddr_un socket_addr = { 0 };
	socket_addr.sun_family = AF_UNIX;
	strncpy(socket_addr.sun_path, socketname.c_str(), socketname.size());
	int retval = bind(connect_socket,
	                  (struct sockaddr *) &socket_addr,
	                  sizeof(socket_addr));
	retval = listen(connect_socket, 20);
	if (retval < 0) {
		perror("bind");
		exit(-1);
	}

	running = true;
	thrd_send = std::make_unique<std::thread>(std::bind(&Communicator::thread_send, this));
	thrd_recv = std::make_unique<std::thread>(std::bind(&Communicator::thread_recv, this));
}

Communicator::~Communicator() {
	running = false;
	thrd_send->join();
	thrd_recv->join();
	close (socketfd );
}

void Communicator::thread_send() {
	while (running) {
		std::string data;
		queue.dequeue(&data);
		if (socketfd != 0) {
			std::cout << "Sending data: " << data << std::endl;
			errno = 0;
			int nsnd = ::send(socketfd, data.c_str(), data.size(), 0);
			std::cout << "Sent " << nsnd << " bytes" << std::endl;
			if (nsnd < 0 || errno != 0) {
				perror("write");
			}
		}
	}
}

void Communicator::thread_recv() {
	char buffer[1024];
	while (running) {
		socketfd = 0;
		socketfd = accept(connect_socket, NULL, NULL);
		if (socketfd == -1) {
			perror("accept");
		}
		std::cout << "Found new client" << std::endl;
		while (running) {
			int nrcv = read(socketfd, buffer, sizeof(buffer));
			if (nrcv < 0) {
				perror("recv");
				break;
			}
			buffer[sizeof(buffer)-1] = '\0';
			execute_command(std::string(buffer));
			buffer[0] = '\0';
		}
	}
}

void Communicator::send(const std::string &data) {
	queue.enqueue(data);
}

void Communicator::inotify (const std::string &operation,
                            const std::string &path) {
	sstrbuf.str("");
	sstrbuf.clear();
	sstrbuf << "{\"op\":\"" << operation
	        << "\",\"path\":\"" << path << "\"}" << std::endl;
	send(sstrbuf.str());
}

void Communicator::register_command(const std::string &command,
                                    const Communicator::command_callback &cb) {
	std::string cmd = command;
	std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
	commands.insert(std::make_pair(command, cb));
}

void Communicator::execute_command(std::string cmd) {
	if (cmd.size() == 0) return;

	std::cout << "Received command: " << cmd << std::endl;
	int pos = cmd.find(':');
	std::string data;
	if (pos != std::string::npos) {
		data = cmd.substr(pos+1);
		cmd = cmd.substr(0, pos);
	}
	// todo use multimap
	std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
	auto it = commands.find(cmd);
	if (it != commands.end()) {
		if (it->second(data)) {
			send("{\"state\":\"success\"}");
		} else {
			send("{\"state\":\"error\",\"error\":\"command failed\"}");
		}
	} else {
		std::cout << "Command not registered: '" << cmd << "'" << std::endl;
		send("{\"state\":\"error\",\"error\":\"unknown command\"}");
	}
}

}
