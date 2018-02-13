#include "api_internal.hh"

#include <unordered_map>

std::unique_ptr<postgres::pool> pgpool;

std::unordered_map<std::string, rainboa::cmd_func> cmd_map;

static aeon::object api_cmd_ping(aeon::object const &, rainboa::persist &) {
	return "pong";
}

void rainboa::init() {
	pgpool.reset( new postgres::pool { "rainboa", std::thread::hardware_concurrency() } );
	
	register_cmd("ping", api_cmd_ping);
	
	auto dbv = pgpool->acquire();
	auth_init(dbv);
}

void rainboa::term() noexcept {
	pgpool.reset();
	cmd_map.clear();
}

void rainboa::register_cmd(std::string const & cmd, rainboa::cmd_func func) {
	cmd_map[cmd] = func;
}

static void process_header(aeon::object const & head, rainboa::persist & pers) {
	//pers.debug = head["debug"].boolean();
	auth_redeem(head, pers);
}

static aeon::object process_cmd(aeon::object const & cmd, rainboa::persist & pers) {
	auto cmd_f = cmd_map.find(cmd["cmd"]);
	if (cmd_f != cmd_map.end()) return cmd_f->second(cmd, pers);
	return aeon::map_t {{"error", "unrecognized command"}};
}

asterid::aeon::object rainboa::process(asterid::aeon::object const & cmd) {
	persist pers { pgpool->acquire() };
	if (cmd.is_array()) {
		process_header(cmd.array()[0], pers);
		aeon::ary_t ret;
		size_t len = cmd.array().size();
		if (len >= 1) {
			ret.resize(len);
			aeon::object info = aeon::map();
			if (pers.acct_id) {
				info["acct_id"] = pers.acct_id;
				info["acct_level"] = pers.acct_level;
				if (!pers.acct_name.empty()) info["acct_name"] = pers.acct_name;
			}
			ret[0] = std::move(info);
			for (size_t i = 1; i < len; i++) {
				aeon::object const & scdm = cmd[i];
				if (!scdm.is_map()) continue;
				ret[i] = process_cmd(scdm, pers);
			}
		}
		return std::move(ret);
	} else return aeon::null;
}
