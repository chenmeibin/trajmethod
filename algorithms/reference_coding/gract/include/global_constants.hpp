//
// Created by adrian on 26/12/17.
//

#ifndef GRACTV2_GLOBAL_CONSTANTS_HPP
#define GRACTV2_GLOBAL_CONSTANTS_HPP

#include <cstdint>

// GraCT's original code assumes a Unix-style global `uint` alias.
// Define it explicitly so MinGW/Windows builds behave the same.
using uint = unsigned int;

#define TIMER 0
#define OBJECT -1
#define SNAPSHOT -2
#define REAPPEARANCE -3
#define DISAPPEARANCE -4
#define REAP_MOVE -5
#define REAP_NO_MOVE -6

#endif //GRACTV2_GLOBAL_CONSTANTS_HPP
