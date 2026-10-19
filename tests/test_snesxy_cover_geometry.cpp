#include "fe_snesxy_cover_aspect.h"
#include <cassert>
#include <cmath>
#include <iostream>

static bool eq(float a,float b) {return std::abs(a-b)<0.005f;}
int main() {
    const float base=448.0f, cap=525.0f;
    // Most US SNES boxes are landscape: width exceeds height, with no crop.
    auto us=fe::SNESXYCaseSize(fe::SNESXYImageRatio(1200,800),base,cap);
    assert(us.width>us.height&&eq(us.width/us.height,1.5f)&&us.width<=cap);
    // Rare Japanese/custom artwork is portrait: must remain tall.
    auto portrait=fe::SNESXYCaseSize(fe::SNESXYImageRatio(700,1024),base,cap);
    assert(portrait.height>portrait.width);
    assert(eq(portrait.width/portrait.height,700.f/1024.f));
    // Tall and wide covers can share the same height basis safely.
    assert(us.height < portrait.height);
    // Square titles are preserved too (no squeezing/cropping).
    auto square=fe::SNESXYCaseSize(1.0f,base,cap);
    assert(eq(square.width,square.height));
    // Center and side cases preserve the same ratio when scaled in 3D.
    auto side=fe::SNESXYCaseSize(1.5f,base,cap,0.88f);
    assert(eq(side.width/side.height,1.5f));
    assert(side.width<us.width);
    // Extreme widescreen artwork shrinks in both axes instead of cropping.
    auto extreme=fe::SNESXYCaseSize(2.65f,base,cap);
    assert(eq(extreme.width/extreme.height,2.65f)&&extreme.width<=cap);
    // No real cover yet: default to an SNES landscape case.
    auto absent=fe::SNESXYCaseSize(fe::SNESXYImageRatio(0,0),base,cap);
    assert(absent.width>absent.height);
    // The square fallback SNES logo is centered (not stretched) in a wide frame.
    auto fallback=fe::SNESXYFitInside(1.f,us.width,us.height);
    assert(eq(fallback.width,fallback.height)&&fallback.width<=us.width&&fallback.height<=us.height);
    assert(eq(fe::SNESXYCaseSize(NAN,base,cap).width,absent.width));
    assert(eq(fe::SNESXYCaseSize(1.5f,base,cap,-1.0f).width,0.f));
    std::cout<<"PASS: SNESXY full-art horizontal/portrait cases, spacing and fallback proportions\n";
}
