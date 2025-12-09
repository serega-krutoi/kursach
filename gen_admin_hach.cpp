#include <iostream>
#include "auth.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <password>\n";
        return 1;
    }

    std::string password = argv[1];
    std::string hash = hashPassword(password);

    std::cout << hash << std::endl;
    return 0;
}
