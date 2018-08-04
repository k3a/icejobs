/*
 * icejobs.cc
 * 
 * Prints maximum number of available Icecream compilation jobs.
 * Based on icetop source code.
 * 
 * Copyright (C) 2016 Adrian Perez <aperez@igalia.com>
 * Copyright (C) 2018 K3A (k3a.me)
 *
 * Distributed under terms of the GPLv2 license.
 */

#include <functional>
#include <poll.h>
#include <set>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <icecc/comm.h>

using host_stats_map = std::unordered_map<std::string, std::string>;

int fdin(int fd, int deadline_ms) {
	struct pollfd f;

	f.fd = fd;
	f.events = POLLIN | POLLERR | POLLHUP;

	int res = poll(&f, 1, deadline_ms /*timeout ms*/);

	if (res < 0) {
		return res;
	} else if (res == 0) {
		return -1; // timout
	}
	return 0;
}

struct icecc_maxjobs_finder {
public:
	enum monitor_state {
		OFFLINE,
		ONLINE,
	};

	icecc_maxjobs_finder() : state(OFFLINE), total_jobs_available(0) {}

	void check_scheduler(bool deleteit = false) {
		if (deleteit) {
			scheduler = nullptr;
		}
		while (!scheduler) {
			std::vector<std::string> names;
			if (!network_name.empty()) {
				names.push_back(network_name);
			} else {
				names.push_back("ICECREAM");
			}
			char *env;
			if ((env = ::getenv("USE_SCHEDULER"))) {
				names.push_back(std::string(env));
			}

			for (auto name : names) {
				auto discover = std::make_unique<DiscoverSched>(name);
				scheduler.reset(discover->try_get_scheduler());
				while (!scheduler && !discover->timed_out()) {
					if (discover->listen_fd() != -1) {
						if (fdin(discover->listen_fd(), 3000)) {
							perror("waiting for socket");
							exit(EXIT_FAILURE);
						}
					} else {
						usleep(50000);
					}
					scheduler.reset(discover->try_get_scheduler());
				}
				if (scheduler) {
					state = ONLINE;
					network_name = discover->networkName();
					scheduler_name = discover->schedulerName();
					scheduler->setBulkTransfer();
					return;
				}
			}
		}
	}

	void listen(int64_t deadline_ms = -1) {
		if (!scheduler->send_msg(MonLoginMsg())) {
			// recheck for the scheduler
			sleep(1);
			check_scheduler(true);
			return;
		}
		while (true) {
			if (fdin(scheduler->fd, deadline_ms)) {
				print_jobs_and_quit();
			}
			while (!scheduler->read_a_bit() || scheduler->has_msg()) {
				if (!handle_activity()) {
					print_jobs_and_quit();
				}
			}
		}
	}

	std::string network_name;
	std::string scheduler_name;
	std::unique_ptr<MsgChannel> scheduler;

private:
	monitor_state state;
	std::set<std::string> known_ips;
	int total_jobs_available;

	bool handle_activity();
	void handle_host_stats(MonStatsMsg &m);
	void print_jobs_and_quit();
};

bool icecc_maxjobs_finder::handle_activity() {
	std::unique_ptr<Msg> m(scheduler->get_msg());
	if (!m) {
		check_scheduler();
		state = OFFLINE;
		return false;
	}

	if (m->type == M_MON_STATS) {
		MonStatsMsg *mm = dynamic_cast<MonStatsMsg *>(m.get());
		if (mm)
			handle_host_stats(*mm);
		return true;
	}

	return false;
}

static host_stats_map parse_stats(const std::string &input) {
	// printf("parsing stats %s\n", input.c_str());
	std::stringstream stream(input);
	std::string key, value;
	host_stats_map stats;
	while (std::getline(stream, key, ':') && std::getline(stream, value)) {
		stats.emplace(key, value);
	}
	return stats;
}

void icecc_maxjobs_finder::handle_host_stats(MonStatsMsg &m) {
	auto stats = parse_stats(m.statmsg);

	std::string ip = stats.at("IP");

	if (known_ips.find(ip) == known_ips.end()) {
		int max_jobs = std::stoul(stats.at("MaxJobs"));
		total_jobs_available += max_jobs;

		known_ips.insert(ip);
	}
}

void icecc_maxjobs_finder::print_jobs_and_quit() {
	printf("%d\n", total_jobs_available);
	exit(0);
}

int main() {
	icecc_maxjobs_finder finder;

	// fprintf(stderr, "Waiting for scheduler...\n");

	finder.check_scheduler();

	while (!finder.scheduler)
		usleep(100000);

	finder.listen(2000);

	return 0;
}
