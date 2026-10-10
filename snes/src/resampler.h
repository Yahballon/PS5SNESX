// (AI-assisted) Continuous stereo rate conversion for the SNES core's native clock.
#pragma once
#include <cstdint>
#include <vector>
namespace snes {
class Resampler {
public:
    void Reset(double input_rate,double output_rate=48000){step=input_rate/output_rate;next=0;primed=false;}
    std::vector<int16_t> Process(const std::vector<int16_t>& input) {
        std::vector<int16_t> output;
        for(size_t i=0;i+1<input.size();i+=2) {
            const int16_t left=input[i],right=input[i+1];
            if(!primed){previous_left=left;previous_right=right;primed=true;output.push_back(left);output.push_back(right);next=step;continue;}
            next-=1;
            while(next<=0){const double fraction=1+next;
                output.push_back(int16_t(previous_left+(left-previous_left)*fraction));
                output.push_back(int16_t(previous_right+(right-previous_right)*fraction));next+=step;}
            previous_left=left;previous_right=right;
        }return output;
    }
private:
    double step=1,next=0;bool primed=false;int16_t previous_left=0,previous_right=0;
};
}
