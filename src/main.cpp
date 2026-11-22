// palmkobo - Palm OS (CloudpilotEmu) front end for Kobo e-readers
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <iostream>
#include <string>

#include "App.h"
#include "Version.h"

namespace {

void usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " [-c palmkobo.ini] [--touch-test] [key=value ...]\n"
              << "\n"
              << "  -c FILE        configuration file (default: palmkobo.ini next to the binary)\n"
              << "  --touch-test   show where touches land, to calibrate touch orientation\n"
              << "  key=value      override any configuration setting\n"
              << "\n"
              << "host testing only:\n"
              << "  --script FILE  scripted touch input: '<ms> down|move|up X Y', '<ms> power', '<ms> quit'\n"
              << "  --max-ms N     quit after N milliseconds\n";
}

std::string defaultConfigPath(const char* argv0) {
    const std::string self(argv0);
    const size_t slash = self.find_last_of('/');
    return (slash == std::string::npos ? std::string(".") : self.substr(0, slash)) + "/palmkobo.ini";
}

}  // namespace

int main(int argc, char** argv) {
    pk::Options options;
    std::vector<std::string> overrides;

    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if ((a == "-c" || a == "--config") && i + 1 < argc) options.configPath = argv[++i];
        else if (a == "--touch-test") options.touchTest = true;
        else if (a == "--script" && i + 1 < argc) options.script = argv[++i];
        else if (a == "--max-ms" && i + 1 < argc) options.maxMs = std::stoll(argv[++i]);
        else if (a == "--version") {
            std::cout << "palmkobo " PALMKOBO_VERSION << std::endl;
            return 0;
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else if (a.find('=') != std::string::npos) overrides.push_back(a);
        else {
            usage(argv[0]);
            return 2;
        }
    }

    pk::Config config;
    if (options.configPath.empty()) options.configPath = defaultConfigPath(argv[0]);
    if (auto err = config.load(options.configPath)) {
        std::cerr << "palmkobo: " << *err << " (using defaults)" << std::endl;
        const size_t slash = options.configPath.find_last_of('/');
        config.baseDir = slash == std::string::npos ? "." : options.configPath.substr(0, slash);
    }
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (auto err = config.set(o.substr(0, eq), o.substr(eq + 1))) {
            std::cerr << "palmkobo: " << *err << std::endl;
            return 2;
        }
    }

    // Unbuffered-ish logging: the Kobo launcher appends stdout to a log file.
    std::cout << std::unitbuf;
    std::cout << "palmkobo " PALMKOBO_VERSION << std::endl;

    pk::App app(config, options);
    return app.run();
}
