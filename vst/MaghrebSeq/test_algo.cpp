// The algorithm routing the patch page draws.
//
// The diagram is only worth having if it is right, and "it looks plausible" is
// not a check. These are facts about the DX7's 32 algorithms that hold whatever
// msfa's table says, plus three algorithms whose shape is well enough known to
// assert outright.
//
// msfa indexes its operators backwards: index 0 is OP6 and index 5 is OP1.

#include "dx_engine.h"

#include <cstdio>
#include <string>
#include <cmath>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

// msfa index -> DX7 operator number
static int opNum(int i) { return 6 - i; }

int main() {
    char msg[400];
    int tgt[6]; bool car[6]; int fb;

    // ---- invariants across all thirty-two ----
    {
        bool everyHasCarrier = true, everyHasFeedback = true;
        bool targetsSane = true, noSelf = true, carriersHaveNoTarget = true;
        std::string bad;
        for (int a = 0; a < 32; a++) {
            mdx::DxEngine::algorithmRouting(a, tgt, car, fb);
            int carriers = 0;
            for (int i = 0; i < 6; i++) if (car[i]) carriers++;
            if (carriers == 0) { everyHasCarrier = false; if (bad.empty()) bad = "alg " + std::to_string(a + 1); }
            if (fb < 0 || fb > 5) everyHasFeedback = false;
            for (int i = 0; i < 6; i++) {
                if (tgt[i] < -1 || tgt[i] > 5) targetsSane = false;
                if (tgt[i] == i) noSelf = false;
                if (car[i] && tgt[i] != -1) carriersHaveNoTarget = false;
            }
        }
        ok("every algorithm has at least one carrier", everyHasCarrier, bad);
        ok("every algorithm has exactly one feedback operator", everyHasFeedback);
        ok("every target is a real operator or none", targetsSane);
        ok("no operator modulates itself", noSelf);
        ok("a carrier has no modulation target", carriersHaveNoTarget);
    }

    // ---- algorithm 1: OP6 feeds back, and OP1 and OP3 are the carriers ----
    {
        mdx::DxEngine::algorithmRouting(0, tgt, car, fb);
        std::string cs;
        for (int i = 0; i < 6; i++) if (car[i]) cs += std::to_string(opNum(i)) + " ";
        snprintf(msg, sizeof msg, "carriers %s/ feedback on OP%d", cs.c_str(), opNum(fb));
        const bool right = car[5] && car[3] && !car[4] && !car[0] && !car[1] && !car[2]
                           && opNum(fb) == 6;
        ok("algorithm 1 is two stacks onto OP1 and OP3, feedback on OP6", right, msg);
    }

    // ---- algorithm 5: three pairs, which is what the derivation uses ----
    {
        mdx::DxEngine::algorithmRouting(4, tgt, car, fb);
        int carriers = 0;
        for (int i = 0; i < 6; i++) if (car[i]) carriers++;
        // OP2 -> OP1, OP4 -> OP3, OP6 -> OP5
        const bool pairs = (tgt[4] == 5) && (tgt[2] == 3) && (tgt[0] == 1);
        std::string cs;
        for (int i = 0; i < 6; i++) if (car[i]) cs += std::to_string(opNum(i)) + " ";
        snprintf(msg, sizeof msg, "%d carriers (%s), OP2>OP1 %d, OP4>OP3 %d, OP6>OP5 %d",
                 carriers, cs.c_str(), (int)(tgt[4] == 5), (int)(tgt[2] == 3), (int)(tgt[0] == 1));
        ok("algorithm 5 is three two-operator stacks", carriers == 3 && pairs && opNum(fb) == 6, msg);
    }

    // ---- algorithm 32: everything is a carrier ----
    {
        mdx::DxEngine::algorithmRouting(31, tgt, car, fb);
        int carriers = 0;
        for (int i = 0; i < 6; i++) if (car[i]) carriers++;
        snprintf(msg, sizeof msg, "%d carriers, feedback on OP%d", carriers, opNum(fb));
        ok("algorithm 32 is six parallel carriers", carriers == 6 && opNum(fb) == 6, msg);
    }

    // ---- out of range must not crash or invent a routing ----
    {
        mdx::DxEngine::algorithmRouting(-3, tgt, car, fb);
        bool clean = (fb == -1);
        for (int i = 0; i < 6; i++) if (car[i] || tgt[i] != -1) clean = false;
        mdx::DxEngine::algorithmRouting(99, tgt, car, fb);
        for (int i = 0; i < 6; i++) if (car[i] || tgt[i] != -1) clean = false;
        ok("an algorithm number out of range yields nothing rather than nonsense", clean);
    }

    // ---- a modulator chain terminates ----
    {
        bool terminates = true;
        std::string bad;
        for (int a = 0; a < 32; a++) {
            mdx::DxEngine::algorithmRouting(a, tgt, car, fb);
            for (int i = 0; i < 6; i++) {
                int at = i, hops = 0;
                while (at >= 0 && !car[at] && hops < 8) { at = tgt[at]; hops++; }
                if (hops >= 8) { terminates = false; if (bad.empty()) bad = "alg " + std::to_string(a + 1); }
            }
        }
        ok("following the wiring always reaches a carrier or a dead end",
           terminates, bad);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
