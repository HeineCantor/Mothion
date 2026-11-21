// A stand-in for Rhubarb Lip Sync in the tests: takes its arguments as Mothion gives them, says its progress on stderr
// as --machineReadable does, and writes fixed mouth cues to the -o file, as JSON. With FAKE_RHUBARB_FAIL set, it
// fails as Rhubarb does. It writes what it was given to the file named by FAKE_RHUBARB_ARGS, if set.

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char **argv)
{
    std::string output;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "-o")
            output = argv[i + 1];
    if (const char *argsFile = std::getenv("FAKE_RHUBARB_ARGS")) {
        if (std::FILE *f = std::fopen(argsFile, "wb")) { // (binary: "\n" alone, on Windows too)
            for (int i = 1; i < argc; ++i)
                std::fprintf(f, "%s\n", argv[i]);
            std::fclose(f);
        }
    }
    std::fprintf(stderr, "{ \"type\": \"start\", \"file\": \"x\", \"log\": { \"level\": \"Info\", \"message\": \"start\" } }\n");
    if (std::getenv("FAKE_RHUBARB_FAIL")) {
        std::fprintf(stderr, "{ \"type\": \"failure\", \"reason\": \"Could not open sound file\", \"log\": {} }\n");
        return 1;
    }
    for (int p : {0, 50, 100})
        std::fprintf(stderr, "{ \"type\": \"progress\", \"value\": %.2f, \"log\": {} }\n", p / 100.0);
    std::FILE *out = std::fopen(output.c_str(), "w");
    if (!out)
        return 2;
    std::fprintf(out, "{ \"metadata\": { \"duration\": 1.00 }, \"mouthCues\": [\n"
                      "  { \"start\": 0.00, \"end\": 0.25, \"value\": \"X\" },\n"
                      "  { \"start\": 0.25, \"end\": 0.50, \"value\": \"D\" },\n"
                      "  { \"start\": 0.50, \"end\": 0.75, \"value\": \"A\" },\n"
                      "  { \"start\": 0.75, \"end\": 1.00, \"value\": \"X\" } ] }\n");
    std::fclose(out);
    std::fprintf(stderr, "{ \"type\": \"success\", \"log\": {} }\n");
    return 0;
}
