#include "api_internal.hh"

#define TUPCHECK if (!res.tuples_ok()) throw rainboa::exception { res.get_error() };
#define CMDCHECK if (!res.cmd_ok()) throw rainboa::exception { res.get_error() };

static void acct_query_info(rainboa::persist & pers) {
	pers.acct_level = 0;
	pers.acct_name = "";
	postgres::result res = pers.dbv.exec_params("SELECT username FROM account.auth WHERE acct_id = $1::BIGINT", {pers.acct_id});
	TUPCHECK
	if (res.num_rows() == 0) { pers.acct_level = 0; return; }
	pers.acct_level = 1;
	pers.acct_name = res(0, 0).string();
}

// ================================
// ACCT_CREATE -- create a new account
// ================================
static aeon::object acct_create(aeon::object const &, rainboa::persist & pers) {
	postgres::result res = pers.dbv.exec("INSERT INTO account.base DEFAULT VALUES RETURNING id");
	TUPCHECK;
	pers.acct_id = res.get_value(0, 0);
	std::string token_name = rainboa::util::random_str(64, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
	std::string token_hash = rainboa::util::hash_blake2b(token_name).hex();
	res = pers.dbv.exec_params("INSERT INTO account.token (acct_id, hash) VALUES ($1::BIGINT, $2::TEXT)", {pers.acct_id, token_hash});
	CMDCHECK;
	acct_query_info(pers);
	aeon::object ret;
	ret["acct_id"] = pers.acct_id;
	ret["acct_level"] = pers.acct_level;
	ret["token"] = token_name;
	return ret;
}

// ================================
// ACCT_CLAIM -- claim an anonymous account
// ================================
static aeon::object acct_claim(aeon::object const & in, rainboa::persist & pers) {
	
	if (!pers.acct_id) return aeon::map_t {{"error", "not authorized, nothing to claim"}};
	postgres::result res = pers.dbv.exec_params("SELECT acct_id FROM account.auth WHERE acct_id = $1::BIGINT", {pers.acct_id});
	TUPCHECK
	if (res.num_rows() != 0) return aeon::map_t {{"error", "this account has already been claimed"}};
	
	std::string username = in["username"].string();
	std::string password = in["password"].string();
	if (!username.size()) return aeon::map_t {{"error", "username required"}};
	if (!password.size()) return aeon::map_t {{"error", "password required"}};
	
	postgres::bigint_t salt = rainboa::util::randomized<postgres::bigint_t>();
	std::string passhash = rainboa::util::hash_blake2b(password + std::to_string(salt)).hex();
	res = pers.dbv.exec_params("INSERT INTO account.auth (acct_id, username, passhash, salt) VALUES ($1::BIGINT, $2::TEXT, $3::TEXT, $4::BIGINT)", {pers.acct_id, username, passhash, salt});
	CMDCHECK;
	
	aeon::object ret;
	acct_query_info(pers);
	ret["acct_id"] = pers.acct_id;
	ret["acct_level"] = pers.acct_level;
	ret["acct_name"] = pers.acct_name;
	return ret;
}

// ================================
// ACCT_AUTH -- login to an account
// ================================
static aeon::object acct_auth(aeon::object const & in, rainboa::persist & pers) {
	std::string username = in["username"].string();
	std::string password = in["password"].string();
	if (!username.size()) return aeon::map_t {{"error", "username required"}};
	if (!password.size()) return aeon::map_t {{"error", "password required"}};
	
	postgres::result res = pers.dbv.exec_params("SELECT acct_id, passhash, salt FROM account.auth WHERE username = $1::TEXT", {username});
	TUPCHECK;
	if (!res.num_fields()) return aeon::map_t {{"error", "unrecognized username"}};
	
	std::string passhash = rainboa::util::hash_blake2b(password + res(0, 2).string()).hex();
	if (passhash != res(0, 1).string()) return aeon::map_t {{"error", "incorrect password"}};
	
	pers.acct_id = res(0, 0);
	std::string token_name = rainboa::util::random_str(64, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
	std::string token_hash = rainboa::util::hash_blake2b(token_name).hex();
	pers.dbv.cmd_params("UPDATE account.auth SET last_login = NOW() WHERE acct_id = $1::BIGINT", {pers.acct_id});
	res = pers.dbv.exec_params("INSERT INTO account.token (acct_id, hash) VALUES ($1::BIGINT, $2::TEXT)", {pers.acct_id, token_hash});
	CMDCHECK;
	
	aeon::object ret;
	acct_query_info(pers);
	ret["acct_id"] = pers.acct_id;
	ret["acct_level"] = pers.acct_level;
	ret["token"] = token_name;
	ret["acct_name"] = pers.acct_name;
	return ret;
}

#define RESCHECK if (!res) throw exception { res.get_error() };

void rainboa::auth_init(postgres::pool::conview & dbv) {
	
	postgres::cmd_result res = dbv.cmd(R"(
		CREATE SCHEMA IF NOT EXISTS account
	)"); RESCHECK
	
	res = dbv.cmd(R"(
		CREATE TABLE IF NOT EXISTS account.base (
			id BIGSERIAL PRIMARY KEY,
			create_date TIMESTAMP NOT NULL DEFAULT NOW()
		)
	)"); RESCHECK
	
	res = dbv.cmd(R"(
		CREATE TABLE IF NOT EXISTS account.auth (
			acct_id BIGINT REFERENCES account.base(id) NOT NULL UNIQUE,
			username VARCHAR(64) NOT NULL UNIQUE,
			passhash CHAR(128) NOT NULL,
			salt BIGINT NOT NULL,
			last_login TIMESTAMP
		)
	)"); RESCHECK
	
	res = dbv.cmd(R"(
		CREATE TABLE IF NOT EXISTS account.token (
			acct_id BIGINT REFERENCES account.base(id) NOT NULL,
			hash CHAR(128) NOT NULL UNIQUE,
			last_use TIMESTAMP
			
		)
	)"); RESCHECK
	
	res = dbv.cmd(R"(
		CREATE INDEX IF NOT EXISTS token_acct_id_idx
		ON account.token(acct_id)
	)"); RESCHECK
	
	register_cmd("acct_create", acct_create);
	register_cmd("acct_claim", acct_claim);
	register_cmd("acct_auth", acct_auth);
}

void rainboa::auth_redeem(aeon::object const & header, persist & pers) {
	std::string token_name = header["token"];
	if (token_name.empty()) return;
	std::string token_hash = util::hash_blake2b(token_name).hex();
	postgres::result res = pers.dbv.exec_params("UPDATE account.token SET last_use = NOW() WHERE hash = $1::TEXT RETURNING acct_id", {token_hash});
	TUPCHECK
	if (res.num_rows() != 1) return;
	pers.acct_id = res.get_value(0, 0);
	acct_query_info(pers);
}
