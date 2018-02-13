#include "psql.hh"

#include <libpq-fe.h>

static void notice (void *, char const *) {}

struct postgres::result::private_data {
	PGresult * res = nullptr;
	ExecStatusType status = PGRES_BAD_RESPONSE;
	~private_data() { if (res) PQclear(res); }
};

postgres::result::result() : data { new private_data } {}
postgres::result::result(pg_result * res) : data { new private_data } {
	data->res = res;
	data->status = res ? PQresultStatus(res) : PGRES_BAD_RESPONSE;
}
postgres::result::result(result && other) : data { other.data.release() } { }
postgres::result::~result() {}

int postgres::result::num_fields() const { return PQnfields(data->res); }
int postgres::result::num_rows() const { return PQntuples(data->res); }
postgres::value postgres::result::get_value(int row, int field) const { char * c = PQgetvalue(data->res, row, field); return c ? c : ""; }
std::string postgres::result::get_error() const { return PQresultErrorMessage(data->res); }
bool postgres::result::cmd_ok() const { return data->status == PGRES_COMMAND_OK; }
bool postgres::result::tuples_ok() const { return data->status == PGRES_TUPLES_OK; }

postgres::result & postgres::result::operator = (result && other) {
	data = std::move(other.data);
	return *this;
}

struct postgres::connection::private_data {
	PGconn * con = nullptr;
	bool ok = false;
	~private_data() { if (con) PQfinish(con); }
};

postgres::connection::connection(std::string const & dbname) : data { new private_data } {
	std::string constr = "user=postgres dbname=" + dbname;
	data->con = PQconnectdb(constr.c_str());
	if (PQstatus(data->con) != CONNECTION_OK) {
		throw postgres::exception { asterid::strf("failed to connect to database \"%s\" (status %i)", dbname.c_str(), PQstatus(data->con)) };
	}
	PQsetNoticeProcessor(data->con, notice, nullptr);
	data->ok = true;
}
postgres::connection::~connection() {}

postgres::result postgres::connection::exec(std::string const & cmd) { return PQexec(data->con, cmd.c_str()); }
postgres::result postgres::connection::exec_params(std::string const & cmd, params_list params) {
	char const * * ptrs = new char const * [params.size()];
	size_t i = 0;
	for (std::string const & str : params) {
		ptrs[i++] = str.data();
	}
	auto result = PQexecParams(data->con, cmd.c_str(), params.size(), nullptr, ptrs, nullptr, nullptr, 0);
	delete [] ptrs;
	return result;
}

bool postgres::connection::ok() { return data->ok; }

postgres::pool::pool(std::string const & dbname, unsigned int num_cons) {
	for (unsigned int i = 0; i < num_cons; i++) {
		cons.emplace_back(new pool_con {dbname, cvm, cv});
		if (!cons.back()->con.ok()) return;
	}
	ok_ = true;
}

postgres::pool::conview postgres::pool::try_acquire() {
	for (auto & pq : cons) {
		if (pq->in_use.test_and_set()) continue;
		return pq;
	}
	return {nullptr};
}

postgres::pool::conview postgres::pool::acquire() {
	while (true) {
		for (auto & pq : cons) {
			if (pq->in_use.test_and_set()) continue;
			return pq;
		}
		std::unique_lock<std::mutex> lk(cvm);
		cv.wait_for(lk, std::chrono::milliseconds(5));
	}
}
