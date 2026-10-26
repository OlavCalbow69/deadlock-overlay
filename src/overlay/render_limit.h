#pragma once
#include <algorithm>
#include <cmath>
namespace overlay {
// Gate painting only. Input/cancellation and the reader keep their normal pace.
struct RenderLimit {
    double deadline{},rate{};
    bool due(double now,double cap,double monitor) {
        if(!std::isfinite(cap)||cap<=0){deadline=rate=0;return true;}
        const double requested=std::min(cap,monitor),period=1000/requested;
        if(requested!=rate){rate=requested;deadline=now;}
        if(now+.001<deadline)return false;
        if(now>deadline+period)deadline=now;
        deadline+=period;return true;
    }
};
}
