#pragma once

#include "api.hh"
#include "psql.hh"

using namespace asterid;

namespace rainboa {
	
	struct persist {
		persist(postgres::pool::conview && cv) : dbv { std::forward<postgres::pool::conview &&>(cv) } {}
		postgres::pool::conview dbv;
		postgres::bigint_t acct_id = 0;
		postgres::bigint_t acct_level = 0;
		std::string acct_name;
	};
	
	typedef aeon::object(*cmd_func)(aeon::object const &, persist &);
	void register_cmd(std::string const & cmd, cmd_func);
	
	void auth_init(postgres::pool::conview & con);
	void auth_redeem(aeon::object const & header, persist &);
	
	namespace util {
		std::string random_str(size_t len, std::string const & chars);
		asterid::buffer_assembly hash_blake2b(std::string str);
		
		void randomize_data(void * ptr, size_t len);
		template <typename T> void randomize(T & v) { randomize_data(reinterpret_cast<void *>(&v), sizeof(T)); }
		template <typename T> T randomized() { T v; randomize<T>(v); return v; }
	}
}
