// Copyright 2017 Johannes Walcher

#include "communicator.h"

#include "mammut_config.h"

#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <deque>
#include <string>
#include <memory>
#include <utility>


namespace mammutfs {

Communicator::Communicator(std::shared_ptr<MammutConfig> config) :
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

	struct sockaddr_un socket_addr;
	memset(&socket_addr, 0, sizeof(socket_addr));
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
	thrd_send = std::make_unique<std::thread>(
		std::bind(&Communicator::thread_send, this));
	thrd_recv = std::make_unique<std::thread>(
		std::bind(&Communicator::thread_recv, this));


	register_void_command("HELP", [this](const std::string &) {
			std::stringstream ss;
			ss << "All supported commands: " << std::endl;
			for (const auto &v : this->commands) {
				ss << v.first << std::endl;
			}
			this->send(ss.str());
		});

	register_void_command("USER", [this](const std::string &) {
			this->send(this->config->username);
		});

	
	register_void_command("CONFIG", [this](const std::string &confkey) {
			std::string str;
			if (this->config->lookupValue(confkey.c_str(), str, true)) {
				this->send(str);
			} else {
				this->send("{'state':'error','error':'could not find config value'}");
			}
		}, "CONFIG:<key>");

	register_command("SETCONFIG", [this](const std::string &kvpair) {
			size_t pos = kvpair.find('=');
			std::string key, value;
			if (pos != std::string::npos) {
				key = kvpair.substr(0, pos);
				value = kvpair.substr(pos+1);
			} else {
				return false;
			}
			this->config.set_value(key, value)
		}, "SETCONFIG:<key>=<value>");
}

Communicator::~Communicator() {
	running = false;
	thrd_send->join();
	thrd_recv->join();
	close(connect_socket);
	for(int i : connected_sockets) {
		close(i);
	}
}

void Communicator::thread_send() {
	while (running) {
		std::string data;
		queue.dequeue(&data);
		if (!connected_sockets.empty()) {
			std::cout << "Sending data: " << data << std::endl;
			errno = 0;
			std::deque<int> to_delete;
			for (int sock : connected_sockets) {
				int nsnd = ::send(sock, data.c_str(), data.size(), 0);
				if (nsnd < 0 || errno != 0) {
					perror("write");
					to_delete.push_back(sock);
				}
			}
			for (int i : to_delete) {
				remove_client(i);
			}
		} else {
			std::cout << "No clients connected. Not sending data" << std::endl;
		}
	}
}

void Communicator::thread_recv() {
	char buffer[1024];
	pollingfd = epoll_create(1);
	if (pollingfd < 0) {
		perror("epoll create");
		exit(-1);
	}

	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = connect_socket;
	if (epoll_ctl(pollingfd, EPOLL_CTL_ADD, connect_socket, &ev) != 0) {
		perror("epoll_ctl");
	}

	struct epoll_event pevents[20];

	while (running) {
		int ready = epoll_wait(pollingfd, pevents, 20, -1);
		if (ready == -1) {
			perror("epoll_wait");
		}

		for (int i = 0; i < ready; ++i) {
			if (pevents[i].data.fd == connect_socket) {
				int socketfd = accept(connect_socket, NULL, NULL);
				std::cout << "Found new client" << std::endl;
				if (socketfd == -1) {
					perror("accept");
					continue;
				}
				ev.events = EPOLLIN | EPOLLET;
				ev.data.fd = socketfd;
				if (epoll_ctl(pollingfd, EPOLL_CTL_ADD, socketfd, &ev) != 0) {
					perror("epoll_ctl");
					continue;
				}
				connected_sockets.push_back(socketfd);
			} else {
				int nrcv = read(pevents[i].data.fd, buffer, sizeof(buffer));
				if (nrcv < 0) {
					perror("recv");
					break;
				}
				buffer[sizeof(buffer)-1] = '\0';
				execute_command(std::string(buffer));
				memset(buffer, 0, sizeof(buffer));
			}
		}
	}
}

void Communicator::remove_client(int sock) {
	for(auto it = connected_sockets.begin();
	    it != connected_sockets.end();
	    ++it) {
		if (*it == sock) {
			connected_sockets.erase(it);
			struct epoll_event ev;
			memset(&ev, 0, sizeof(ev));
			ev.data.fd = sock;
			if (epoll_ctl(pollingfd, EPOLL_CTL_DEL, sock, &ev) != 0) {
				perror("epoll_ctl");
			}
			break;
		}
	}
}

void Communicator::send(const std::string &data) {
	queue.enqueue(data);
}

void Communicator::inotify(const std::string &operation,
                           const std::string &path) {
	sstrbuf.str("");
	sstrbuf.clear();
	sstrbuf << "{\"op\":\"" << operation
	        << "\",\"path\":\"" << path << "\"}" << std::endl;
	send(sstrbuf.str());
}


void Communicator::register_command(const std::string &unfiltered_command,
                                    const Communicator::command_callback &cb,
                                    const std::string &helptext) {
	std::string str = unfiltered_command;
	std::transform(str.begin(), str.end(), str.begin(), ::toupper);
	command cmd;
	cmd.callback = cb;
	if (helptext == "") {
		cmd.helptext = unfiltered_command;
	} else {
		cmd.helptext = helptext;
	}

	this->commands.insert(std::make_pair(str, cmd));
}


void Communicator::execute_command(std::string cmd) {
	if (cmd.size() == 0) return;

	std::cout << "Received command: " << cmd << std::endl;
	size_t pos = cmd.find(':');
	std::string data;
	if (pos != std::string::npos) {
		data = cmd.substr(pos+1);
		cmd = cmd.substr(0, pos);
	}
	// todo use multimap
	std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
	auto it = commands.find(cmd);
	if (it != commands.end()) {
		if (it->second.callback(data)) {
			send("{\"state\":\"success\"}");
		} else {
			send("{\"state\":\"error\",\"error\":\"" + cmd + "\",}");
		}
	} else {
		std::cout << "Command not registered: '" << cmd << "'" << std::endl;
		send("{\"state\":\"error\",\"error\":\"unknown command\"}");
	}
}

}  // namespace mammutfs
