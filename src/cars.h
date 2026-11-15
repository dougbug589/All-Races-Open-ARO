// cars.h - racer cars (generated from game data) and approximate real-world performance.
#pragma once

namespace cars {

enum Class { Sports, Muscle, Exotic, GrandTourer, Everyday, SUV, Race, ClassCount };
struct Car { int id; int cls; const char* name; };

const Car* find(int id);
const Car* all(int* count);
const char* className(int cls);
int powerToWeight(int id);          // hp per tonne, 0 if unknown

}  // namespace cars
