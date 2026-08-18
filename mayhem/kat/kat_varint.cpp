// kat_varint.cpp -- KAT probe over Fossilize's own varint codec (varint.hpp/varint.cpp), the
// binary encoding SPIR-V payloads use inside a serialized blob (see fossilize.cpp's
// parse_shader_modules() varint_buffer path). Fixed input -> exact encoded size + exact
// round-tripped values, asserted unconditionally (SPEC 6.3).
#include "varint.hpp"

#include <cstdio>
#include <cstdint>
#include <vector>

using namespace Fossilize;

int main()
{
	static const uint32_t words[] = { 0, 1, 127, 128, 16383, 16384, 2097151, 2097152, 268435455, 268435456 };
	const size_t count = sizeof(words) / sizeof(words[0]);

	size_t encoded_size = compute_size_varint(words, count);

	std::vector<uint8_t> buffer(encoded_size);
	encode_varint(buffer.data(), words, count);

	std::vector<uint32_t> decoded(count, 0xFFFFFFFFu);
	bool decode_ok = decode_varint(decoded.data(), count, buffer.data(), buffer.size());

	bool roundtrip_ok = decode_ok;
	for (size_t i = 0; roundtrip_ok && i < count; i++)
		if (decoded[i] != words[i])
			roundtrip_ok = false;

	printf("KAT_VARINT_ENCODED_SIZE=%zu\n", encoded_size);
	printf("KAT_VARINT_ROUNDTRIP_OK=%d\n", roundtrip_ok ? 1 : 0);
	fflush(stdout);

	return (encoded_size == 26 && roundtrip_ok) ? 0 : 1;
}
