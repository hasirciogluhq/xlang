#include "xlang/util.h"

#include <cstdlib>
#include <string>

namespace xlang {

int runCommand(std::string_view command) {
    const std::string owned(command);
    return std::system(owned.c_str());
}

} // namespace xlang
