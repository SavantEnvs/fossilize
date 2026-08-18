// fuzz_foz_db.cpp -- the Fossilize DATABASE CONTAINER surface.
//
// Fossilize's ".foz" stream-archive format (fossilize_db.cpp) is the on-disk container Steam's
// shader pre-caching reads untrusted from disk: a header, an entry table, and per-entry
// offset/size/compression metadata. This harness feeds fuzzer bytes straight into that parser via
// create_stream_archive_database() + DatabaseInterface::prepare()/get_hash_list_for_resource_tag()/
// read_entry() -- the exact sequence every Fossilize consumer (fossilize-replay, the Vulkan layer,
// fossilize-list, ...) uses to open an archive it did not create.
//
// NO VULKAN DEVICE. This target never touches VkInstance/VkDevice -- it only exercises the
// container/format layer (header parsing, entry table walk, miniz decompression of blob payloads),
// which is exactly the untrusted-input surface a malicious/corrupted .foz file attacks.
//
// FILE-PATH PROBLEM (SPEC 6.2 item 13): the DatabaseInterface API is filename-based, not
// buffer-based, and Mayhem mounts the image read-only + runs the target from a cwd we don't
// control. /dev/shm is the one writable location guaranteed present, so we truncate-write the
// fuzzer bytes there each iteration and open THAT path -- never a relative path, /tmp, or an
// absolute path under the image directory.
//
// BOUNDS (SPEC 6b): a malformed .foz can declare an enormous entry count or blob size. We cap the
// raw input size, the number of hashes enumerated per resource tag, and the bytes read per entry,
// so one pathological archive cannot stall the whole campaign chasing a multi-GB "allocate this
// much" declaration. Genuine crashes/OOM inside those bounds are NOT masked.
#include "fossilize_db.hpp"

#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <vector>
#include <unistd.h>

using namespace Fossilize;

// A handful of MB is plenty to reach every code path (header + several entries); bigger inputs
// mostly just mean bigger internal buffers for the same coverage.
static constexpr size_t kMaxInputSize = 4 * 1024 * 1024;

// Guards against a maliciously huge declared entry count / blob size turning one input into an
// unbounded allocation-and-read loop (a hang, which stops the whole campaign -- SPEC 6b). These
// are generous relative to any legitimate small Vulkan object blob.
static constexpr size_t kMaxEntriesPerTag = 4096;
static constexpr size_t kMaxBlobBytes = 16 * 1024 * 1024;

// /dev/shm is the only writable location guaranteed present regardless of how/where Mayhem mounts
// the image (SPEC 6.2 item 13). A PID-suffixed name keeps concurrent workers from colliding.
static const char *ScratchPath()
{
	static char path[64];
	if (path[0] == '\0')
		snprintf(path, sizeof(path), "/dev/shm/fuzz_foz_db.%d.foz", (int)getpid());
	return path;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size == 0 || size > kMaxInputSize)
		return 0;

	const char *path = ScratchPath();
	FILE *f = fopen(path, "wb");
	if (!f)
		return 0;
	size_t written = fwrite(data, 1, size, f);
	fclose(f);
	if (written != size)
	{
		remove(path);
		return 0;
	}

	DatabaseInterface *db = create_stream_archive_database(path, DatabaseMode::ReadOnly);
	if (db)
	{
		if (db->prepare())
		{
			for (int tag = 0; tag < RESOURCE_COUNT; tag++)
			{
				size_t num_hashes = 0;
				if (!db->get_hash_list_for_resource_tag(static_cast<ResourceTag>(tag), &num_hashes, nullptr))
					continue;
				if (num_hashes == 0)
					continue;
				if (num_hashes > kMaxEntriesPerTag)
					num_hashes = kMaxEntriesPerTag;

				std::vector<Hash> hashes(num_hashes);
				size_t actual = num_hashes;
				if (!db->get_hash_list_for_resource_tag(static_cast<ResourceTag>(tag), &actual, hashes.data()))
					continue;
				if (actual > hashes.size())
					actual = hashes.size();

				for (size_t i = 0; i < actual; i++)
				{
					size_t blob_size = 0;
					if (!db->read_entry(static_cast<ResourceTag>(tag), hashes[i], &blob_size, nullptr,
					                    PAYLOAD_READ_NO_FLAGS))
						continue;
					if (blob_size == 0 || blob_size > kMaxBlobBytes)
						continue;

					std::vector<uint8_t> blob(blob_size);
					db->read_entry(static_cast<ResourceTag>(tag), hashes[i], &blob_size, blob.data(),
					              PAYLOAD_READ_NO_FLAGS);
				}
			}
		}
		delete db;
	}

	remove(path);
	return 0;
}
