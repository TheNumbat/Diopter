
#pragma once

#include <rpp/base.h>

#include "pbrt.h"

using namespace rpp;

namespace RPLY {

PBRT::Mesh load(String_View directory, String_View filename);

}
