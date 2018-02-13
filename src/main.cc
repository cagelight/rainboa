#include <csignal>
#include <atomic>
#include <asterid/strops.hh>
#include <locust/locust.hh>

#include "api.hh"

#define log asterid::streamlogger { "WSTEST: ", [](std::string const & str){ std::cout << str << std::endl; } }

#define NUM_WORKERS 4

struct rainboa_exchange : public locust::dummy_exchange {
	virtual bool process_header(locust::http::request_header const * header) override {
		req_head = header;
		
		if (req_head->content_length() > upload_limit) return false;
		return true;
	}
	virtual void body_segment(asterid::buffer_assembly const & buf) override {
		req_body << buf;
	}
	virtual void process(locust::http::response_header & header, asterid::buffer_assembly & body) override {
		
		if (req_head->method == "OPTIONS") {
			header.code = locust::http::status_code::ok;
			std::string origin = req_head->field("Origin");
			if (!origin.empty()) header.fields["Access-Control-Allow-Origin"] = origin;
			std::string headers = req_head->field("Access-Control-Request-Headers");
			if (!origin.empty()) header.fields["Access-Control-Allow-Headers"] = headers;
			header.fields["Access-Control-Allow-Methods"] = "OPTIONS POST";
			return;
		}
		
		header.fields["Access-Control-Allow-Origin"] = "*";
		
		if (req_head->method != "POST") {
			header.code = locust::http::status_code::method_not_allowed;
			header.fields["Content-Type"] = "text/plain; charset=UTF-8";
			body << u8"A weird snake bites you and deals ∞ physical and ∞ poison damage, you die. 🐍";
			return;
		}
		
		if (!req_head->content_length()) {
			header.code = locust::http::status_code::bad_request;
			header.fields["Content-Type"] = "text/plain; charset=UTF-8";
			body << u8"It's empty... there's nothing here... what are you playing at? 🐍";
			return;
		}
		
		if (req_head->content_type() != "application/json" && req_head->content_type() != "application/aeon") {
			header.code = locust::http::status_code::unsupported_media_type;
			header.fields["Content-Type"] = "text/plain; charset=UTF-8";
			body << u8"The heck is this? I can't eat this. 🐍";
			return;
		}
		
		asterid::aeon::object rec;
		bool return_aeon = false;
		
		try {
			if (req_head->content_type() == "application/aeon") {
				asterid::buffer_assembly req_body_copy {req_body};
				rec = asterid::aeon::object::parse_binary(req_body_copy);
				return_aeon = true;
			} else {
				rec = asterid::aeon::object::parse_text(req_body.to_string());
			}
		} catch (asterid::aeon::exception::parse &) {
			header.code = locust::http::status_code::bad_request;
			header.fields["Content-Type"] = "text/plain; charset=UTF-8";
			body << u8"You got wax in your internet tubes? 🐍";
			return;
		}
		
		asterid::aeon::object ret = rainboa::process(rec);
		
		header.code = locust::http::status_code::ok;
		if (return_aeon) {
			header.fields["Content-Type"] = "application/aeon";
			ret.serialize_binary(body);
		} else {
			header.fields["Content-Type"] = "application/json";
			body << ret.serialize_text();
		}
	}
private:
	static constexpr size_t upload_limit = 131072;
	locust::http::request_header const * req_head;
	asterid::buffer_assembly req_body;
};

static std::atomic_bool run_sem {true};

static void handle_signal(int sig) {
	switch (sig) {
		default:
			return;
		case SIGINT:
			if (run_sem) run_sem.store(false);
			else std::terminate();
	}
}

int main() {
	signal(SIGINT, handle_signal);
	try {
		rainboa::init();
		asterid::cicada::server sv {false, NUM_WORKERS};
		sv.listen<locust::protocol<rainboa_exchange>>(8081);
		sv.master( [](){return run_sem.load();} );
		rainboa::term();
	} catch(...) {
		log << "unknown exception occurred, cannot continue";
	}
	return 0;
}
