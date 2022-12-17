//
// Created by nmckibben on 12/14/22.
//

#ifndef HIGHS_HIGHSCLBK_H
#define HIGHS_HIGHSCLBK_H

#include "HighsInt.h"

struct HighsClbkInfo {
    explicit HighsClbkInfo(const HighsInt& iteration)
      : iteration_(iteration) {}
    const HighsInt& iteration_;
};
typedef void(*HighsClbk)(HighsClbkInfo*);

#endif  // HIGHS_HIGHSCLBK_H
