#include "uci.h"
#include "Engine.h"
#include "Search.h"
#include "MoveGeneration.h"
#include "Evaluation.h"
#include "types.h"
#include <iostream>
int main() {
    std::cout << "Sigma-chess By Mazhaoze && V0.1\n";
    Engine engine;
    uci::loop(engine);
    return 0;
}