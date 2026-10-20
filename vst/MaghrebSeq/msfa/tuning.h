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

// Reduced from Dexed's tuning.h for this plug-in.
//
// The original carries SCL and KBM loading built on the Surge tuning library
// and reports errors through JUCE. Neither is wanted here: msfa itself only
// ever calls midinote_to_logfreq and is_standard_tuning, and the microtuning
// in this family lives in Maghreb Tubu's tabʿ tables rather than in a scale
// file. Cutting the rest removes two dependencies and changes no behaviour
// that the engine can observe.

#ifndef __SYNTH_TUNING_H
#define __SYNTH_TUNING_H

#include "synth.h"
#include <memory>
#include <string>

class TuningState {
public:
    virtual ~TuningState() { }

    virtual int32_t midinote_to_logfreq(int midinote) = 0;
    virtual bool is_standard_tuning() { return true; }
    virtual int scale_length() { return 12; }
    virtual std::string display_tuning_str() { return "Standard Tuning"; }
};

std::shared_ptr<TuningState> createStandardTuning();

#endif
