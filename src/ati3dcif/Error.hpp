#pragma once

#include "ati3dcif/ATI3DCIF.h"

#include <stdexcept>
#include <string>

namespace glrage {
namespace cif {

class Error : public std::runtime_error {
public:
    Error(const char *message, C3D_EC errorCode);
    Error(const std::string &message, C3D_EC errorCode);
    Error(const char *message);
    Error(const std::string &message);
    C3D_EC getErrorCode() const;

private:
    C3D_EC m_errorCode;
};

}
}
