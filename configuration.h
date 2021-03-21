#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_

#include <string>
#include <memory>

class Configuration {
public:
    struct Parameters {
        std::string folder;
        unsigned long period;
    };

    static const std::unique_ptr<Configuration> &GetInstance();
    const Parameters &GetParameters() const;

private:
    Configuration();

    static std::unique_ptr<Configuration> instance;
    Parameters parameters;
};

#endif // CONFIGURATION_H_