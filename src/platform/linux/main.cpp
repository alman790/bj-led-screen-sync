#include "lib/platform/linux/app/LinuxApp.hpp"

#include <string>

int main(int argc, char** argv) {
    LinuxApp app;
    return app.run(argc > 1 ? argv[1] : std::string(), argc > 2 ? argv[2] : std::string("auto"));
}
