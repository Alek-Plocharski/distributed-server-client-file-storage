#include "client.h"

int main(int argc, const char* argv[]) {

    client_options options;
    options.fill_from_arguments(argc, argv);
    Client client(options);
    client.run();

    return 0;
}