// tympanis_pack_bake — packs a baked drum (JSON, written by tools/bake_kit.py) into the compact
// form the plugin embeds (plugin/PackedBake.h). CMake runs it on every bake in resources/bake/
// when it builds the plugin's binary data.
//
//   tympanis_pack_bake IN.bake.json OUT.bake.pack
//
// It checks its own output before writing it: the packed form must unpack to the same JSON
// tree, bit for bit, and that tree must load (kit.bake.json as the kit header, any other bake as
// a drum). Otherwise the build fails here.
#include "PackedBake.h"
#include "dsp/KitData.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: tympanis_pack_bake IN.bake.json OUT.bake.pack\n");
        return 2;
    }
    const auto in = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    const auto name = in.getFileName().toStdString();
    try
    {
        const auto root = tympanis::json::parseFile (in.getFullPathName().toStdString());
        const auto packed = tympanis::packed::pack (root);
        const auto back = tympanis::packed::unpack (packed.getData(), packed.getSize(), name);
        if (! tympanis::packed::identical (root, back))
            throw std::runtime_error (name + ": the packed bake does not unpack to the same JSON");
        if (name == "kit.bake.json")
        {
            tympanis::KitData kit;
            std::vector<std::string> keys;
            tympanis::parseKitHeader (back, kit, keys);
        }
        else
        {
            (void) tympanis::parseDrum (back);
        }
        if (! out.replaceWithData (packed.getData(), packed.getSize()))
            throw std::runtime_error ("cannot write " + out.getFullPathName().toStdString());
        std::printf ("%s: %lld bytes of JSON, %lld bytes packed\n", name.c_str(), (long long) in.getSize(),
                     (long long) packed.getSize());
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf (stderr, "tympanis_pack_bake: %s\n", e.what());
        return 1;
    }
}
