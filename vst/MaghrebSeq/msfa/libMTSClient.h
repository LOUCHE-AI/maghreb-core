// Minimal stand-in for the MTS-ESP client header.
//
// msfa's dx7note.cc calls exactly two MTS functions, and only when an MTS-ESP
// master is present. This plug-in does not offer MTS-ESP - its tuning comes
// from the tabʿ tables in Maghreb Tubu, not from a system-wide master - so
// the client is stubbed to report that there is no master. That keeps the
// vendored surface to the Apache-2.0 msfa folder alone, with no extra
// dependency to fetch or ship.
//
// If MTS-ESP is ever wanted, replace this file with the real libMTSClient
// from https://github.com/ODDSound/MTS-ESP and link its implementation; msfa
// needs no other change.

#ifndef LIBMTSCLIENT_H_STUB
#define LIBMTSCLIENT_H_STUB

#include <cmath>

typedef struct MTSClient MTSClient;

inline MTSClient* MTS_RegisterClient() { return nullptr; }
inline void MTS_DeregisterClient(MTSClient*) {}

inline bool MTS_HasMaster(MTSClient*) { return false; }

// Never reached while HasMaster is false, but a sane 12-TET answer beats
// returning zero if a future msfa version calls it unguarded.
inline double MTS_NoteToFrequency(MTSClient*, char midinote, char /*channel*/) {
    return 440.0 * std::pow(2.0, (static_cast<double>(midinote) - 69.0) / 12.0);
}
inline bool MTS_ShouldFilterNote(MTSClient*, char, char) { return false; }

#endif
