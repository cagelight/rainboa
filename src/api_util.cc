#include "api_internal.hh"

#include <botan/hash.h>
#include <botan/auto_rng.h>

static Botan::AutoSeeded_RNG rng {};
static std::unique_ptr<Botan::HashFunction> blake2b = Botan::HashFunction::create("Blake2b");
static std::mutex b2bmut;

static size_t randrange(size_t min, size_t max) { // [min,max)
	size_t i, r = max - min;
	rng.randomize(reinterpret_cast<uint8_t *>(&i), sizeof(i));
	return min + (i % r);
}

std::string rainboa::util::random_str(size_t len, std::string const & chars) {
	std::string ret;
	for (size_t i = 0; i < len; i++) {
		ret += chars[randrange(0, chars.size())];
	}
	return ret;
}

asterid::buffer_assembly rainboa::util::hash_blake2b(std::string str) {
	std::unique_lock<std::mutex> lk {b2bmut};
	auto buf = blake2b->process(str);
	lk.unlock();
	asterid::buffer_assembly bb {};
	bb.resize(buf.size());
	memcpy(bb.data(), buf.data(), bb.size());
	return bb;
}

void rainboa::util::randomize_data(void * ptr, size_t len) {
	rng.randomize(reinterpret_cast<uint8_t *>(ptr), sizeof(len));
}
