// Copyright 2017 Johannes Walcher

#include "communicator.h"

#include "mammut_config.h"

#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <limits.h>

#include <algorithm>
#include <deque>
#include <string>
#include <memory>
#include <utility>
#include <syslog.h>

namespace mammutfs {

Communicator::Communicator(std::shared_ptr<MammutConfig> config) :
	config(config),
	socket(-1),
	connected(false) {
	config->lookupValue("daemon_socket", this->socketname);

	register_void_command("HELP", [this](const std::string &, std::string &resp) {
			std::stringstream ss;
			ss << "{\"commands\":[";

			int cnt = this->commands.size();
			for (const auto &v : this->commands) {
				ss << "\"" << v.first << "\"";
				if (cnt -- > 1)
					ss << ",";
			}
			ss << "]}";
			resp = ss.str();
		});

		register_void_command("USER", [this](const std::string &, std::string &resp) {
				std::stringstream ss;
				ss << "\"" << this->config->username() << "\"";
				resp = ss.str();
		});

	register_void_command("CONFIG", [this](const std::string &confkey,
	                                       std::string &resp) {
			std::string str;
			if (this->config->lookupValue(confkey.c_str(), str, true)) {
				std::stringstream ss;
				ss << "{\"value\":\"" << str << "\"}";
				resp = ss.str();
			} else {
				resp = "{\"state\":\"error\",\"error\":\"could not find config value\"}";
			}
		}, "CONFIG:<key>");

	register_command("SETCONFIG", [this](const std::string &kvpair, std::string &resp) {
			size_t pos = kvpair.find('=');
			std::string key, value;
			if (pos != std::string::npos) {
				key = kvpair.substr(0, pos);
				value = kvpair.substr(pos+1);
				this->config->set_value(key, value);
				return true;
			} else {
				resp = "\"invalid config, expecing key=value\"";
				return false;
			}
		}, "SETCONFIG:<key>=<value> - will only work for certain enabled keys");
}

Communicator::~Communicator() {
	running = false;
	thrd_comm->join();
	close(this->socket);
}

void Communicator::start() {
	this->running = true;
	this->thrd_comm = std::make_unique<std::thread>(
		std::bind(&Communicator::communication_thread, this));
}

bool Communicator::connect(bool initial_attempt) {
	if (this->socket != -1) { // The socket is still connected?
		close(this->socket);
		socket = -1;
	}
	this->connected = false;
	this->socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (this->socket < 0) {
		char buffer[1024] = {0};
		strcat(buffer, "socket failed: socket: ");
		strcat(buffer, strerror(errno));
		syslog(LOG_ERR, buffer);
		fprintf(stderr, buffer);
		return false;
	}

	struct sockaddr_un socket_addr;
	memset(&socket_addr, 0, sizeof(socket_addr));
	socket_addr.sun_family = AF_UNIX;
	strncpy(socket_addr.sun_path, socketname.c_str(),
	        std::min(int(socketname.size()), PATH_MAX));

	int retval = ::connect(this->socket, (struct sockaddr *) &socket_addr,
	                     sizeof(socket_addr));
	if (retval < 0) {
		if (!initial_attempt && errno == ENOENT) {
			return false;
		}
		char buffer[1024] = {0};
		strcat(buffer, "failed to connect: ");
		strcat(buffer, strerror(errno));
		syslog(LOG_ERR, buffer);
		fprintf(stderr, buffer);
		fprintf(stderr, "\n");
		return false;
	}

	// Send our hello!
	sstrbuf.str("");
	sstrbuf.clear();
	sstrbuf << "{\"op\":\"hello\",\"user\":\"" << this->config->username() << "\","
	        << "\"mountpoint\":\"" << this->config->mountpoint() << "\"}\n";
	send_command(sstrbuf.str());

	this->connected = true;
	return true;
}

void Communicator::communication_thread() {
	int pollingfd = epoll_create(1);
	if (pollingfd < 0) {
		perror("epoll create");
		exit(-1);
	}

	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = this->queue.get_eventfd();
	if (epoll_ctl(pollingfd, EPOLL_CTL_ADD, this->queue.get_eventfd(), &ev) != 0) {
		perror("main - epoll_ctl");
	}

	static const int num_events = 2;
	struct epoll_event pevents[num_events];

	while (running) {
		int connect_backoff = 1024;
		bool initial_connection = true;
		while(!this->connected) {
			if (this->connect(initial_connection)) {
				break;
			}
			initial_connection = false;
			// This will be done only after the connection was established,
			// so this should never occur, only if the server died!
			// Wait until the connection is established again
			// which is a exponentially rising number until max. 5 seconds
			usleep(connect_backoff);
			connect_backoff = std::min(1*1000*1000, connect_backoff * 2);
		}

		ev.data.fd = this->socket;
		if (epoll_ctl(pollingfd, EPOLL_CTL_ADD, this->socket, &ev) != 0) {
			perror("main - epoll_ctl");
		}

		while (this->connected) {
			if (!queue.empty()) {
				// Fake an activation
				uint64_t buffer = 1;
				if (::write(this->queue.get_eventfd(), &buffer, 8) < 0) {
					perror("Event write");
					continue;
				}
			}

			int ready = epoll_wait(pollingfd, pevents, num_events, -1);
			if (ready == -1 && errno != EINTR) {
				perror("epoll_wait");
			}

			for (int i = 0; i < ready && this->connected; ++i) {
				if (pevents[i].data.fd == this->socket) {
					receive_command();
				} else if (pevents[i].data.fd == this->queue.get_eventfd()) {
					if (this->queue.empty()) {
						continue;
					}
					uint8_t buffer[8];
					// We have the input event pending
					if (::read(this->queue.get_eventfd(), buffer, 8) < 0) {
						perror("eventfd");
						continue;
					}

					std::string data;
					if (!this->queue.dequeue(&data, false)) {
						// We could not retrieve a value - this should never occure, we
						// check nevertheless
						continue;
					}
					send_command(data);
				}
			}
		}
		ev.data.fd = this->socket;
		if (epoll_ctl(pollingfd, EPOLL_CTL_DEL, this->socket, &ev) != 0) {
			perror("main - epoll_ctl");
		}
	}
}


void Communicator::receive_command() {
	char buffer[1024];
	memset(buffer, 0, sizeof(buffer));
	int nrcv = ::read(this->socket, buffer, sizeof(buffer));
	if (nrcv < 0) {
		perror("recv");
		// if in doubt - disconnect
		this->connected = false;
		return;
	} else if (nrcv == 0) {
		// disconnected!
		this->connected = false;
		return;
	}

	buffer[sizeof(buffer)-1] = '\0';
	execute_command(std::string(buffer));
}


void Communicator::send_command(const std::string &data) {
	int nsnd = ::send(this->socket, data.c_str(), data.size(), 0);
	if (nsnd < 0 && errno != 0) {
		perror("write");
	}
}

void Communicator::send(const std::string &data) {
	if (data[data.size() - 1] != '\n') {
		std::stringstream ss;
		ss << data;
		ss << "\n";
		this->queue.enqueue(ss.str());
	} else {
		this->queue.enqueue(data);
	}
}

void Communicator::inotify(const std::string &operation,
                           const std::string &module,
                           const std::string &path,
                           const std::string &path2) {
	sstrbuf.str("");
	sstrbuf.clear();
	sstrbuf << "{\"op\":\"" << operation << "\","
	        << "\"module\":\"" << module << "\","
	        << "\"path\":\"" << path << "\"";
	if (path2 != "") {
		sstrbuf << ", \"path2\":\"" << path2 << "\"";
	}
	sstrbuf << "}" << std::endl;
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
		sstrbuf.str("");
		sstrbuf.clear();
		std::string response;
		if (it->second.callback(data, response)) {
			if (response.empty()) response = "\"\"";
			sstrbuf << "{\"state\":\"success\","
			        << "\"response\":" << response
			        << "}";
			send(sstrbuf.str());
		} else {
			if (response.empty()) response = "\"\"";
			sstrbuf << "{\"state\":\"error\","
			        << "\"cmd\":\"" + cmd + "\","
			        << "\"response\":" << response << "}";
			send(sstrbuf.str());
		}
	} else {
		std::cout << "Command not registered: '" << cmd << "'" << std::endl;
		send("{\"state\":\"error\",\"error\":\"unknown command\"}");
	}
}

}  // namespace mammutfs
