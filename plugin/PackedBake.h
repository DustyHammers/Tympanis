#pragma once

// The compact form of a baked drum that the plugin embeds (docs/BUILDS.md). The JSON bakes in
// resources/bake/ stay the source (tools/bake_kit.py writes them): at build time tools/pack_bake
// packs each one, and the plugin unpacks it to the same JSON tree, which parseDrum() and
// parseKitHeader() (plugin/dsp/KitData.h) turn into the same KitData as the JSON would give.
//
// Cymbalis's packed bake (its plugin/PackedBake.h), format 1, with Tympanis's own magic.
// Little-endian:
//   0   8 bytes  "TYMPPACK"
//   8   u32      format version (1)
//   12  u32      0 (reserved)
//   16  u64      payload size, uncompressed
//   24  u64      payload checksum: FNV-1a 64 of the uncompressed payload
//   32  ...      the payload, zlib-compressed (juce::GZIPCompressorOutputStream)
// The payload is the JSON tree, one value after another, each led by a tag byte: null, false,
// true; a number (f64); a string (u32 length, UTF-8 bytes); an array (u32 count, values); an
// object (u32 count, then per member u32 key length, key bytes, value). An array of numbers
// only is stored whole, as f64 or, when every value is exactly an int32, as i32. Every number
// keeps its exact bits.

#include <juce_core/juce_core.h>

#include "dsp/Json.h"

#include <string>

namespace tympanis::packed
{

// The resource name of a bake's packed form: tom1.bake.json -> tom1.bake.pack.
juce::String resourceName (const juce::String& bakeFile);

// The packed form of a bake's JSON tree.
juce::MemoryBlock pack (const json::Value& root);

// The JSON tree of a packed bake. Throws std::runtime_error, naming `name`, when the data is not
// a packed bake, has another format version, or fails its checksum.
json::Value unpack (const void* data, size_t size, const std::string& name);

// The same tree, bit for bit: the same types, member order, strings and number bits.
bool identical (const json::Value& a, const json::Value& b);

} // namespace tympanis::packed
