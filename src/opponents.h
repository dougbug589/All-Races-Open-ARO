// opponents.h - which cars race against you.
#pragma once

namespace opponents {

bool install();

enum Mode { SameClass = -1, RealWorld = -2 };   // or a class number 0..6
int  mode();
void setMode(int m);

}  // namespace opponents
