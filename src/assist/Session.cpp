#include "Session.h"
namespace ia2a {
bool Session::offer(std::uint64_t ticket,const Settings& settings,float gain) {
    if(!current(ticket)||!validSettings(settings)||!std::isfinite(gain)) return false;
    proposed=settings; compensation=gain; preview=true; working=false;
    return true;
}
void Session::invalidate() {
    ++generation; proposed.reset(); preview=false; working=false; compensation=0;
}
void Session::listen(bool candidate,bool matched) { preview=candidate&&pending(); match=matched; }
Monitor Session::monitor() const {
    return {saved,seed(),pending(),preview,preview&&match?compensation:0.0f};
}
bool Session::apply() {
    if(!canApply()) return false;
    previous=saved; saved=*proposed; invalidate(); return true;
}
bool Session::undo() {
    if(!previous) return false;
    saved=*previous; previous.reset(); invalidate(); return true;
}
void Session::restore(const Settings& settings) {
    invalidate(); previous.reset(); saved=settings;
}
}
