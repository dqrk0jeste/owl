#include "util.h"
#include <math.h>

inline double distance(double ax, double ay, double bx, double by) {
  return sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
}
