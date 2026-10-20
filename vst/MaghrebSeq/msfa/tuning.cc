/*
 * Copyright 2016-2025 Pascal Gauthier.
 * Copyright 2012 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Reduced from Dexed's tuning.cc: the standard tuning only. The SCL and KBM
// paths it also carried are built on the Surge tuning library and report
// errors through JUCE, and msfa never calls into them. The table below is the
// original's arithmetic, unchanged.

#include "tuning.h"

struct StandardTuning : public TuningState {
    StandardTuning() {
        const int base = 50857777;  // (1 << 24) * (log(440) / log(2) - 69/12)
        const int step = (1 << 24) / 12;
        for (int mn = 0; mn < 128; ++mn)
            current_logfreq_table_[mn] = base + step * mn;
    }

    virtual int32_t midinote_to_logfreq(int midinote) override {
        if (midinote < 0) midinote = 0;
        if (midinote > 127) midinote = 127;
        return current_logfreq_table_[midinote];
    }

    int current_logfreq_table_[128];
};

std::shared_ptr<TuningState> createStandardTuning() {
    return std::make_shared<StandardTuning>();
}
