# fuzz_state_json: missing "version" key aborts via a failed rapidjson assertion

**Reproducer:** `missing-version-key.json` (contents: `{}`) -- replay with
`/mayhem/fuzz_state_json-standalone mayhem/fuzz_state_json/known-findings/missing-version-key.json`.

**Cause.** `StateReplayer::Impl::parse()` (fossilize.cpp) does:

```cpp
Document doc;
doc.Parse(reinterpret_cast<const char *>(buffer), json_size);
if (doc.HasParseError()) { ...; return false; }

int version = doc["version"].GetInt();   // <-- no doc.HasMember("version") check
```

Every other top-level field the function reads (`applicationInfo`, `shaderModules`, `samplers`, ...)
is guarded with `doc.HasMember(...)` first; `"version"` is not. rapidjson's
`Value::operator[](const char*)` on a missing member hits `RAPIDJSON_ASSERT(false)`
(`rapidjson/document.h`), which in a normal (NDEBUG-less, or assert-enabled) build calls `abort()`.

**Impact.** Any syntactically-valid JSON object that merely omits the top-level `"version"` key --
the simplest possible malformed Fossilize blob, `{}` -- aborts the process instead of being
rejected with `return false` the way every other missing/malformed field is. This is a trivial
DoS-by-crash on the deserializer's very first field check, reachable from both fuzz targets that
route through `StateReplayer::parse()` (`fuzz_state_json` directly, and `fuzz_foz_db` indirectly for
any RESOURCE_* blob it manages to read out of a `.foz` container and hand to the replayer).

**Why not masked here.** SPEC 6b: genuine crashes are real findings and must not be guarded away in
the harness. This one fires on a large fraction of malformed inputs (anything missing "version"),
so Mayhem will report it very quickly and very often -- that's expected, not a harness bug. We did
NOT patch `fossilize.cpp` (upstream files are never edited by the integration) and did NOT add a
pre-check in the harness that would suppress the crash.

**One-line upstream fix (not applied here):** guard the read the same way every other field is:

```cpp
if (!doc.HasMember("version") || !doc["version"].IsInt())
    return false;
int version = doc["version"].GetInt();
```

Kept out of `mayhem/<target>/testsuite/` deliberately (that corpus is replayed every run; a
crashing seed there would abort every future run before it explores anything else).
