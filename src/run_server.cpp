#include "server.h"

int main(int argc, const char* argv[]) {

    server_options options;
    options.fill_from_arguments(argc, argv);
    Server server(options);
    server.run();

    return 0;
}
