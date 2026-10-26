#pragma once
#include <cmath>
namespace Control {
// Both display sides mirror these same three levels. Inputs are milliseconds.
inline unsigned deltaLights(double deltaMs,double stepMs,bool valid=true){
  if(!valid||!std::isfinite(deltaMs)||!std::isfinite(stepMs)||stepMs<=0)return 0;
  return unsigned(std::fmin(3.0,std::floor(std::fabs(deltaMs)/stepMs)));
}
}
