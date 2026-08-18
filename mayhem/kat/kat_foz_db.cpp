// kat_foz_db.cpp -- KAT probe for fuzz_foz_db's exact code path (SPEC 6.3 anti-reward-hacking).
//
// Fossilize's own test/ suite is a homegrown abort()-on-failure framework: it exits 0 on success
// with NO stdout assertion. Under verify-repo's sabotage shim (LD_PRELOAD _exit(0) before main()
// on every non-system binary), a suite like that "passes" trivially -- the process never even
// reaches the code that would abort(). So this probe exists to print, and mayhem/test.sh to grep,
// EXACT values through a dynamically-linked binary the shim CAN silence: if the shim fires, no
// KAT_* line is ever printed, and the grep -qxF assertions in test.sh fail loudly instead of
// reporting a false pass.
//
// Round-trips a small, fixed dataset through the real create_stream_archive_database() writer AND
// reader -- the same container-format code (.foz header/entry-table/blob layer) fuzz_foz_db feeds
// untrusted bytes into -- and asserts the exact counts/sizes read back.
#include "fossilize_db.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

using namespace Fossilize;

static const char kShaderBlob[] = "KAT-SHADER-PAYLOAD";
static const char kSamplerBlob[] = "AB";
static const Hash kShaderHash = 0xdeadbeefULL;
static const Hash kSamplerHash = 0x1ULL;

int main()
{
	char path[64];
	snprintf(path, sizeof(path), "/tmp/kat_foz_db.%d.foz", (int)getpid());
	remove(path);

	DatabaseInterface *writer = create_stream_archive_database(path, DatabaseMode::OverWrite);
	if (!writer || !writer->prepare())
	{
		fprintf(stderr, "KAT_FOZ: failed to create/prepare writer database\n");
		return 1;
	}
	bool write_ok = true;
	write_ok = write_ok && writer->write_entry(RESOURCE_SHADER_MODULE, kShaderHash, kShaderBlob,
	                                           sizeof(kShaderBlob) - 1, PAYLOAD_WRITE_NO_FLAGS);
	write_ok = write_ok && writer->write_entry(RESOURCE_SAMPLER, kSamplerHash, kSamplerBlob,
	                                           sizeof(kSamplerBlob) - 1, PAYLOAD_WRITE_NO_FLAGS);
	writer->flush();
	delete writer;
	if (!write_ok)
	{
		fprintf(stderr, "KAT_FOZ: write_entry failed\n");
		remove(path);
		return 1;
	}

	DatabaseInterface *reader = create_stream_archive_database(path, DatabaseMode::ReadOnly);
	if (!reader || !reader->prepare())
	{
		fprintf(stderr, "KAT_FOZ: failed to create/prepare reader database\n");
		remove(path);
		return 1;
	}

	size_t shader_count = 0, sampler_count = 0;
	reader->get_hash_list_for_resource_tag(RESOURCE_SHADER_MODULE, &shader_count, nullptr);
	reader->get_hash_list_for_resource_tag(RESOURCE_SAMPLER, &sampler_count, nullptr);

	size_t shader_size = 0;
	bool read_shader = reader->read_entry(RESOURCE_SHADER_MODULE, kShaderHash, &shader_size, nullptr,
	                                      PAYLOAD_READ_NO_FLAGS);

	char shader_buf[128];
	bool shader_bytes_ok = false;
	if (read_shader && shader_size > 0 && shader_size <= sizeof(shader_buf))
	{
		size_t sz = shader_size;
		if (reader->read_entry(RESOURCE_SHADER_MODULE, kShaderHash, &sz, shader_buf, PAYLOAD_READ_NO_FLAGS) &&
		    sz == sizeof(kShaderBlob) - 1 && memcmp(shader_buf, kShaderBlob, sz) == 0)
			shader_bytes_ok = true;
	}

	delete reader;
	remove(path);

	printf("KAT_FOZ_SHADER_COUNT=%zu\n", shader_count);
	printf("KAT_FOZ_SAMPLER_COUNT=%zu\n", sampler_count);
	printf("KAT_FOZ_SHADER_BYTES=%zu\n", shader_size);
	printf("KAT_FOZ_SHADER_CONTENT_OK=%d\n", shader_bytes_ok ? 1 : 0);
	fflush(stdout);

	bool ok = shader_count == 1 && sampler_count == 1 && shader_size == sizeof(kShaderBlob) - 1 && shader_bytes_ok;
	return ok ? 0 : 1;
}
