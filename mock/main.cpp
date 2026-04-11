#include "mock_server.h"
#include <cstdlib>
#include <iostream>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int port = (argc == 2) ? std::atoi(argv[1]) : 9001;

    lambda_runtime::mock::MockServer server(port);
    server.start();

    std::cout
        << "\n=== Mini Lambda Mock Server ===\n\n"
        << "Step 1 – open a second terminal and run the handler:\n"
        << "  AWS_LAMBDA_RUNTIME_API=127.0.0.1:" << port << " ./examples/hello_world\n\n"
        << "Step 2 – open a third terminal and invoke it:\n"
        << "  curl -s -X POST http://127.0.0.1:" << port
        << "/invoke \\\n"
        << "       -H 'Content-Type: application/json' \\\n"
        << "       -d '{\"name\": \"AWS\"}'\n\n"
        << "The response will appear here. Press Ctrl+C to stop.\n\n";

    // Block until Ctrl+C sends SIGINT and terminates the process.
    // ~MockServer() will clean up the socket and threads.
    pause();

    return 0;
}
