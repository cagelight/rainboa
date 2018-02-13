#pragma once

#include <asterid/aeon.hh>

#include <stdexcept>

namespace rainboa {
	
	void init();
	void term() noexcept;
	
	typedef std::runtime_error exception;
	
	asterid::aeon::object process(asterid::aeon::object const &);
	
}
