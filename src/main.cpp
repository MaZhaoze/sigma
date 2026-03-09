#include "uci.h"
#include "Engine.h"
#include "Search.h"
#include "MoveGeneration.h"
#include "Evaluation.h"
#include "types.h"
int main() {
    Engine engine;
    uci::loop(engine);
    return 0;
}