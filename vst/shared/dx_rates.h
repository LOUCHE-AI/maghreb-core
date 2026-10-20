// Seconds for a 60 dB decay at each DX7 envelope rate, measured.
//
// The plug-ins that edit patches keep their own copy in dx_patch.h; this one
// exists because the shared browser classifies voices by their envelopes and
// TubuDX has no dx_patch.h to borrow from.
//
// ponytail: three copies of a hardware constant in the tree now. Fold the
// other two into this when something next touches dx_patch.h.

#pragma once

namespace mdx {

static const float kEnvelopeRateSeconds[100] = {
    58.04989f, 58.04989f, 58.04989f, 58.04989f, 58.04989f,
    58.04989f, 58.04989f, 58.04989f, 58.04989f, 58.04989f,
    58.04989f, 52.96544f, 52.96544f, 46.56036f, 46.56036f,
    37.22522f, 30.91011f, 30.91011f, 26.65506f, 23.31501f,
    11.60998f, 11.60998f, 11.60998f, 11.60998f, 11.60998f,
    11.59982f, 11.59982f,  9.40481f,  9.40481f,  7.84980f,
     6.70476f,  6.70476f,  5.83474f,  4.64472f,  4.64472f,
     3.91474f,  3.36472f,  3.36472f,  2.94966f,  2.94966f,
     2.35465f,  1.94467f,  1.94467f,  1.66966f,  1.48463f,
     1.48463f,  1.20961f,  0.98467f,  0.98467f,  0.84463f,
     0.75465f,  0.75465f,  0.61460f,  0.61460f,  0.51955f,
     0.43465f,  0.43465f,  0.38458f,  0.29968f,  0.29968f,
     0.24961f,  0.23438f,  0.23438f,  0.19955f,  0.15456f,
     0.15456f,  0.13932f,  0.13932f,  0.10957f,  0.10449f,
     0.10449f,  0.07982f,  0.06458f,  0.06458f,  0.05950f,
     0.05442f,  0.05442f,  0.04426f,  0.04426f,  0.03483f,
     0.02975f,  0.02975f,  0.02467f,  0.02395f,  0.02395f,
     0.01959f,  0.01887f,  0.01887f,  0.01451f,  0.01379f,
     0.01379f,  0.01016f,  0.01016f,  0.00943f,  0.00871f,
     0.00871f,  0.00798f,  0.00726f,  0.00726f,  0.00508f,
};

} // namespace mdx
