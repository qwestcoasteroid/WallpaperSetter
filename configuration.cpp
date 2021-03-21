#include "configuration.h"

#include <fstream>
#include <regex>
#include <algorithm>

#define FOLDER_PARAMETER "folder"
#define PERIOD_PARAMETER "period"

std::unique_ptr<Configuration> Configuration::instance{};

Configuration::Configuration() {
    std::ifstream stream(L"config.ini");
    std::regex regularExpression(R"(folder=.*|period=.*)",
        std::regex_constants::extended);

    std::string line;
    size_t position{};

    while (!stream.eof()) {
        std::getline(stream, line);

        if (std::regex_match(line, regularExpression)) {
            position = line.find(FOLDER_PARAMETER);
            if (position != std::string::npos) {
                parameters.folder = line.substr(
                    std::strlen(FOLDER_PARAMETER) + 1);
                continue;
            }
            position = line.find(PERIOD_PARAMETER);
            if (position != std::string::npos) {
                parameters.period = std::stoul(line.substr(
                    std::strlen(PERIOD_PARAMETER) + 1));
            }
        }
    }

    stream.close();
}

const std::unique_ptr<Configuration> &Configuration::GetInstance() {
    if (instance == nullptr) {
        instance = std::unique_ptr<Configuration>(new Configuration);
    }

    return instance;
}

const Configuration::Parameters &Configuration::GetParameters() const {
    return parameters;
}