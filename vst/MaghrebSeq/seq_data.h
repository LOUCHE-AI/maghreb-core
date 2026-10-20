// Maghreb Seq - the data.
//
// Generated from the decoded module data, not retyped: the 90 rhythms come
// from the Move's mseq-dsp.so by way of the Max for Live device, and the
// eleven strokes from maghreb_fm.c. Regenerate with gen_seq_data.py rather
// than editing this file.

#pragma once

namespace mseq {

static const int kNumRhythms = 90;
static const int kNumStrokes = 11;
static const int kNumKits    = 3;
static const int kTotalSteps = 1494;   // across every rhythm

// Every rhythm sits on a 16th-note grid; its length is its true cycle, so
// the odd meters are not padded out to a bar.
static const char* const kRhythmNames[kNumRhythms] = {
    "rufaa'aya", "afr-mawlawii-1", "afr-mawlawii-2",
    "dawr-rawaan-mawlawii-1", "dawr-rawaan-mawlawii-2", "niim-dawr-kabiir-mawlawii",
    "s'adaawi-1", "s'adaawi-2", "s'adaawi-3",
    "TN-sadaawi", "mannuubiyya", "TN-mannuubiyya",
    "haddaawii-1", "haddaawii-2", "haddaawii-3",
    "haddaawii-4", "buhalla", "TN-buhalla",
    "silsila-al-musaddar", "btaaihii-tunisia", "btaaihii-algeria-1",
    "btaaihii-algeria-2", "btaaihii-morocco", "suufyaan-algeria",
    "insiraaf-1", "insiraaf-2", "insiraaf-3",
    "khatim", "khatiim", "silsila",
    "khlas", "khlas-algeria", "shabia-morocco-heart",
    "shabia-morocco-lung", "DZ-bashraf", "DZ-musaddar-1",
    "DZ-musaddar-2", "DZ-musaddar-3", "DZ-btaaihii-1",
    "DZ-btaaihii-2", "DZ-bataaihii", "DZ-darj",
    "DZ-darij-al-awwal", "DZ-darij-ath-thaanii", "DZ-suufyaan",
    "DZ-khalaas-ath-thaanii", "DZ-khalaas-al-awwal", "DZ-khlas",
    "DZ-mudawwar-al-hawzii-1", "DZ-mudawwar-al-hawzii-2", "DZ-sufyaan",
    "DZ-allaawii", "DZ-gumbahii", "TN-musaddar-12-4",
    "TN-musaddar-12-8", "TN-abyaat-1", "TN-abyaat-2",
    "TN-btaaihii", "TN-mashad", "TN-nawakht-1",
    "TN-nawakht-2", "TN-dkhuul-al-barwal", "TN-darij",
    "TN-khafiif", "TN-khatim", "TN-khatiim",
    "TN-silsila", "TN-samaaii-taaiir", "TN-mujarrad",
    "TN-murabba-2", "TN-fezzaani-al-beduui", "TN-murabba-al-beduui",
    "TN-fezzaanii-1", "TN-fezzaanii-2", "TN-fezzaanii-thaanii",
    "TN-fezzaanii-thaalath", "TN-harbii", "TN-alaajjii",
    "TN-halla", "TN-ghiita-1", "TN-ghiita-2",
    "TN-bunawwaara", "TN-murabba-1", "TN-suuga",
    "TN-djerbi", "TN-jerbi", "TN-qerqenii",
    "TN-qerqenii-as-sarii", "TN-buziiqa", "TN-bunawwarat-aqrabii",
};

static const char* const kRhythmMeters[kNumRhythms] = {
    "4/8", "9/4 4+2+3", "9/4 4+2+3", "14/8 3+4+3+4",
    "14/8 3+4+3+4", "14/4", "12/8", "12/8",
    "12/8", "12/8", "12/8", "12/8",
    "12/8 4+3+3+2", "12/8 3+4+3+2", "12/8 3+4+3+2", "12/8 4+3+3+2",
    "6/8", "6/8", "6/4 3+3", "8/8 3+2+3",
    "4/8", "4/8", "8/8 3+2+3", "19/8 7+7+5",
    "5/8", "5/8", "5/8", "6/8",
    "3/4", "3/8", "3/8", "3/8",
    "12/8", "12/8", "4/4", "4/4",
    "8/8 3+3+2", "6/4 3+3", "4/8", "4/8",
    "4/8", "6/8", "8/16", "4/4",
    "19/8 7+7+5", "3/4", "6/8", "3/8",
    "6/8 3+3", "6/8 3+3", "7/8 3+4", "2/4",
    "2/4", "12/4 5+4+3", "12/8", "8/8 3+2+3",
    "8/8 4+4", "8/8 3+2+3", "8/8 5+3", "7/4 3+2+2",
    "7/4 3+2+2", "4/4", "6/4", "6/4",
    "6/8", "3/4", "3/8", "3/8",
    "5/8", "4/4", "4/4", "8/8",
    "4/4", "4/4", "8/8 4+4", "8/16",
    "4/4", "6/8", "6/8", "6/8",
    "6/8", "8/8", "4/4", "4/4",
    "2/4", "4/4", "4/4", "2/4",
    "2/4", "2/4",
};

// One character per 16th. '.' is a rest; the letters are the strokes below.
static const char* const kRhythms[kNumRhythms] = {
    "D.D.T.T.",
    "D...T...T.......D...D...T...T.......",
    "D...T.k.T...k...D.m.D...T...T.......",
    "D.....T...T...D.D.m.T...T...",
    "D.T.k.D.m.T.k.DmD.m.T.k.T.k.",
    "D.TD.TD.TDT.T.T.TkDDTTDDTkTk",
    "D.....T.....D...........",
    "D...D...T.T.D...T.T...T.",
    "D.....T.....D...T.T.T.T.",
    "D.....T.....D...........",
    "D...T...T.T...D...T...T.",
    "D...T...T.T...D...T...T.",
    "....T...D.....T...T.D...",
    "....T.T.D.....T...T.D...",
    "....T.T.D...D.T...T.D...",
    "D...T...D.....T...T.D...",
    "D.....T...T.",
    "D.....T...T.",
    "D...T.T.T...T...T.......",
    "D..T..T...D.T...",
    "D.TkT.T.",
    "D.TkD.T.",
    "D..D.TDTD.T.DD.T",
    "D...T.D...T.T.D...T.D...T.T.D...TkT.T.",
    "D.T...D...",
    "DDT...T...",
    "T.T.D..D.T",
    "D.T.T.T.T.T.",
    "D...T.......",
    "D.T.T.",
    "D.tT..",
    "D.D.TT",
    "T.K.T.K.D.K.T.K.T.K.D.K.",
    "G...D...K.G...K.D.......",
    "D.T...T.T...T...",
    "D.......T...T...",
    "T.D...T.D...D...",
    "D...T.T.T.TTT.T.T...T.T.",
    "D.TkT.T.",
    "D.TkD.T.",
    "D.T.D..T",
    "D.D...T.T...",
    "DT.TD.T.",
    "D...T.T.D...T...",
    "D...T.D...T.T.D...T.D...T.T.D...TkT.T.",
    "D...T...T.T.",
    "D...D.D...T.",
    "D.D.TT",
    "D...D.T...T.",
    "D.TkD.T.TkTk",
    "DT.TT.DT.tT.T.",
    "D.DT.DT.",
    "T..TT.D.",
    "D.......T...T...T...T.T.T...T...T.......T...T...",
    "D...T.T.T.TTT.T.T...T.T.",
    "D.T.D.T.T.T.T.T.",
    "D.TkD.T.D.T.T.T.",
    "D..T..T...D.T...",
    "D.T...T...T.T...",
    "D.......T...D.......T...T...",
    "D...T.......D...D...T.......",
    "D.D.....T...T.T.",
    "D...D...T...T...T...T.T.",
    "D.......T...T...T.......",
    "D.T.T.T.T.T.",
    "D...T.......",
    "D.T.T.",
    "D.T.T.",
    "D...T.T...",
    "T.D...T.D...T...",
    "D.T...T.D...T...",
    "D.TkD.T.TkD.T.Tk",
    "D.T...T...T.T...",
    "D.T.T.T.D.T.T.Tk",
    "D.D...T...T.T...",
    "DDTTDDTk",
    "D...T.D.T...T.T.",
    "D...D.T...T.",
    "T.D...T.T...",
    "D.D.S...T.T.",
    "D.D.T...T.T.",
    "D...T...T.T...T.",
    "D.TkD.T.T.TkT.T.",
    "D...T.T.....T.T.",
    "D..T..T.",
    "D...T.D.T...T.T.",
    "T.D...T.D...T.T.",
    "T..TD.T.",
    "TT..D...",
    "DT..D.T.",
};

// D d m T t k K S s G r
static const char kStrokeChars[kNumStrokes + 1] = "DdmTtkKSsGr";
static const char* const kStrokeNames[kNumStrokes] = {
    "doum", "soft doum", "mute", "tak", "light tak", "ka", "strong ka", "slap", "soft slap", "gak", "roll"
};

// Two-operator FM plus a band-passed noise layer, exactly as the module:
// freq, ratio, index, fmDecay, ampDecay, bend, bendDecay, noise, noiseFreq,
// noiseQ, noiseDecay, gain
struct StrokeDef {
    float freq, ratio, index, fmDecay, ampDecay;
    float bend, bendDecay;
    float noise, noiseFreq, noiseQ, noiseDecay;
    float gain;
};
static const StrokeDef kStrokes[kNumStrokes] = {
    { 78.0f, 1.41f, 0.85f, 0.045f, 0.42f, 1.1f, 0.03f, 0.14f, 320.0f, 1.1f, 0.07f, 1.0f },   // D
    { 76.0f, 1.41f, 0.55f, 0.04f, 0.33f, 0.85f, 0.028f, 0.1f, 300.0f, 1.1f, 0.055f, 0.8f },   // d
    { 92.0f, 2.1f, 0.45f, 0.018f, 0.085f, 0.6f, 0.014f, 0.16f, 480.0f, 1.4f, 0.03f, 0.62f },   // m
    { 420.0f, 3.2f, 1.3f, 0.016f, 0.095f, 0.55f, 0.01f, 0.55f, 2600.0f, 2.2f, 0.045f, 0.86f },   // T
    { 445.0f, 3.2f, 0.9f, 0.012f, 0.062f, 0.4f, 0.008f, 0.45f, 2900.0f, 2.4f, 0.03f, 0.62f },   // t
    { 610.0f, 4.1f, 0.8f, 0.01f, 0.05f, 0.35f, 0.007f, 0.6f, 3600.0f, 2.6f, 0.026f, 0.55f },   // k
    { 590.0f, 4.1f, 1.2f, 0.013f, 0.07f, 0.5f, 0.009f, 0.66f, 3400.0f, 2.5f, 0.034f, 0.74f },   // K
    { 300.0f, 5.7f, 2.3f, 0.02f, 0.13f, 0.9f, 0.012f, 0.9f, 1900.0f, 1.3f, 0.09f, 0.92f },   // S
    { 320.0f, 5.7f, 1.5f, 0.016f, 0.09f, 0.7f, 0.01f, 0.72f, 2100.0f, 1.4f, 0.06f, 0.68f },   // s
    { 240.0f, 4.8f, 1.7f, 0.014f, 0.07f, 0.75f, 0.01f, 0.72f, 1500.0f, 1.6f, 0.04f, 0.72f },   // G
    { 520.0f, 6.3f, 1.1f, 0.03f, 0.15f, 0.3f, 0.02f, 0.85f, 3000.0f, 1.2f, 0.12f, 0.6f },   // r
};

// Each kit scales frequency, decay, noise and FM index.
struct KitDef { const char* name; float freq, decay, noise, index; };
static const KitDef kKits[kNumKits] = {
    { "Bendir", 1.0f, 1.0f, 1.0f, 1.0f },
    { "Darbuka", 1.28f, 0.72f, 0.62f, 1.25f },
    { "Tar", 1.62f, 0.55f, 0.78f, 0.85f },
};

} // namespace mseq
