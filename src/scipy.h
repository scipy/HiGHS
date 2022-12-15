//
// Created by nmckibben on 12/14/22.
//

#ifndef HIGHS_SCIPY_H
#define HIGHS_SCIPY_H

namespace ipx {
  class Info;
}

namespace scipy {
  typedef void(*clbk_t)(ipx::Info*);
}

#include "ipm.h"

#endif  // HIGHS_SCIPY_H
