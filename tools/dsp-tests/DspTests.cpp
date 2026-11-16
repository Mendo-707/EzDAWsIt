#include "Engine/Dsp.h"   // run with tools/dsp-tests/run-linux.sh
#include <cstdio>
using namespace ez::dsp;
static void sine (juce::AudioBuffer<float>& b, double sr, float hz, float amp) { for (int c=0;c<b.getNumChannels();++c) for (int i=0;i<b.getNumSamples();++i) b.setSample(c,i, amp*std::sin(2*M_PI*hz*i/sr)); }
static float peakOf (juce::AudioBuffer<float>& b, int from=0) { float p=0; for(int c=0;c<b.getNumChannels();++c) for(int i=from;i<b.getNumSamples();++i) p=std::max(p,std::abs(b.getSample(c,i))); return p; }
static bool finite (juce::AudioBuffer<float>& b){ for(int c=0;c<b.getNumChannels();++c) for(int i=0;i<b.getNumSamples();++i) if(!std::isfinite(b.getSample(c,i))) return false; return true; }
static int fails=0;
#define CHECK(x, ...) do { if(!(x)) { ++fails; printf("FAIL %s: ", #x); printf(__VA_ARGS__); printf("\n"); } else printf("ok   %s\n", #x); } while(0)
static void runBlocks (auto&& fn, juce::AudioBuffer<float>& b, int block=512){ for(int s=0;s<b.getNumSamples();s+=block){ int n=std::min(block,b.getNumSamples()-s); float* ch[2]={b.getWritePointer(0)+s, b.getNumChannels()>1?b.getWritePointer(1)+s:nullptr}; fn(ch,b.getNumChannels(),n);} }
// zero-crossing pitch estimate
static float pitchOf (juce::AudioBuffer<float>& b, double sr, int from){ int z=0; int first=-1,last=-1; for(int i=from+1;i<b.getNumSamples();++i){ if(b.getSample(0,i-1)<0 && b.getSample(0,i)>=0){ if(first<0) first=i; last=i; ++z; } } return z>1? (float)((z-1)*sr/(last-first)) : 0; }
int main(){
  const double sr=48000;
  { Gate g; g.prepare(sr); g.thresholdDb=-30; juce::AudioBuffer<float> b(2,48000); sine(b,sr,200,0.003f); runBlocks([&](float* const* c,int n,int k){g.process(c,n,k);},b);
    CHECK(peakOf(b,24000) < 0.0001f, "quiet signal gated %g", peakOf(b,24000));
    juce::AudioBuffer<float> l(2,48000); sine(l,sr,200,0.5f); runBlocks([&](float* const* c,int n,int k){g.process(c,n,k);},l);
    CHECK(peakOf(l,24000) > 0.45f, "loud passes %g", peakOf(l,24000)); }
  { Compressor c; c.prepare(sr); c.thresholdDb=-20; c.ratio=4; c.kneeDb=0; juce::AudioBuffer<float> b(2,48000); sine(b,sr,200,1.0f); runBlocks([&](float* const* ch,int n,int k){c.process(ch,n,k);},b);
    float expected = dbToGain(-20 + 20.0f/4); CHECK(std::abs(peakOf(b,24000)-expected) < expected*0.2f, "0 dB in, 4:1 at -20 -> about -15 dB: %g vs %g", peakOf(b,24000), expected); }
  { Limiter lim; lim.prepare(sr,512); lim.ceilingDb=-1; lim.inputDb=6; juce::AudioBuffer<float> b(2,48000); sine(b,sr,100,0.9f); runBlocks([&](float* const* ch,int n,int k){lim.process(ch,n,k);},b);
    CHECK(peakOf(b) <= dbToGain(-1)+1e-5f, "never over the ceiling %g", peakOf(b)); CHECK(peakOf(b,24000) > dbToGain(-1)*0.9f, "but close to it %g", peakOf(b,24000)); CHECK(finite(b),"finite"); }
  { DeEsser d; d.prepare(sr); d.thresholdDb=-40; juce::AudioBuffer<float> hi(2,48000), lo(2,48000); sine(hi,sr,7000,0.5f); sine(lo,sr,200,0.5f);
    runBlocks([&](float* const* ch,int n,int k){d.process(ch,n,k);},hi); DeEsser d2; d2.prepare(sr); d2.thresholdDb=-40; runBlocks([&](float* const* ch,int n,int k){d2.process(ch,n,k);},lo);
    CHECK(peakOf(hi,24000) < 0.3f, "esses turned down %g", peakOf(hi,24000)); CHECK(peakOf(lo,24000) > 0.45f, "lows untouched %g", peakOf(lo,24000)); }
  { Saturator s; s.prepare(sr); s.driveDb=24; juce::AudioBuffer<float> b(2,48000); sine(b,sr,200,0.9f); runBlocks([&](float* const* ch,int n,int k){s.process(ch,n,k);},b); CHECK(finite(b),"finite"); CHECK(peakOf(b,24000) < 1.5f, "bounded %g", peakOf(b,24000)); }
  { Utility u; u.mono=true; juce::AudioBuffer<float> b(2,100); for(int i=0;i<100;++i){b.setSample(0,i,1);b.setSample(1,i,0);} float* ch[2]={b.getWritePointer(0),b.getWritePointer(1)}; u.process(ch,2,100); CHECK(std::abs(b.getSample(0,50)-0.5f)<1e-6 && std::abs(b.getSample(1,50)-0.5f)<1e-6,"mono %g %g",b.getSample(0,50),b.getSample(1,50)); }
  { Gate g; g.prepare(sr); g.thresholdDb=-40; g.holdMs=0; juce::AudioBuffer<float> b(2,48000); sine(b,sr,200,0.5f); runBlocks([&](float* const* c,int n,int k){g.process(c,n,k);},b);
    CHECK(peakOf(b,24000) > 0.45f, "hold 0 still opens %g", peakOf(b,24000)); }
  { Limiter lim; lim.prepare(sr,64); lim.ceilingDb=-3; lim.inputDb=12; lim.releaseMs=10; juce::Random r(1); juce::AudioBuffer<float> b(2,192000); for(int c=0;c<2;++c) for(int i=0;i<b.getNumSamples();++i) b.setSample(c,i,(r.nextFloat()*2-1)*0.9f);
    runBlocks([&](float* const* ch,int n,int k){lim.process(ch,n,k);},b,64); CHECK(peakOf(b) <= dbToGain(-3)+1e-5f, "noise, fast release: never over %g", peakOf(b)); }
  for (double rate : { 44100.0, 96000.0, 192000.0 })
    for (float hz : { 98.0f, 110.0f, 220.0f })
    { PitchCorrector p; p.prepare(rate); p.amount=1; p.speedMs=5; float hzOff = hz*1.02f; juce::AudioBuffer<float> b(1,(int)(rate*2)); sine(b,rate,hzOff,0.5f); runBlocks([&](float* const* ch,int n,int k){p.process(ch,n,k);},b,64);
      float out = pitchOf(b,rate,(int)rate); CHECK(std::abs(out-hz) < hz*0.006f, "%g Hz at %g kHz: %g Hz out (detected %g)", hzOff, rate/1000, out, p.getDetectedHz()); }
  { PitchCorrector p; p.prepare(sr); p.amount=0; juce::AudioBuffer<float> b(1,48000); sine(b,sr,300,0.5f); juce::AudioBuffer<float> orig(b); runBlocks([&](float* const* ch,int n,int k){p.process(ch,n,k);},b);
    int L=p.getLatencySamples(); float err=0; for(int i=L+1000;i<48000;++i) err=std::max(err,std::abs(b.getSample(0,i)-orig.getSample(0,i-L))); CHECK(err<0.01f, "amount 0: the input, %d samples late %g", L, err); }
  for (float hz : { 150.0f, 220.0f, 310.0f })
  { PitchCorrector p; p.prepare(sr); p.amount=1; p.speedMs=5; juce::AudioBuffer<float> b(1,96000); sine(b,sr,hz,0.5f); runBlocks([&](float* const* ch,int n,int k){p.process(ch,n,k);},b);
    float midi = 69+12*std::log2(hz/440.0f); float target = 440*std::pow(2.0f,(std::round(midi)-69)/12.0f);
    float out = pitchOf(b,sr,48000); CHECK(std::abs(out-target) < target*0.006f, "%g Hz -> %g Hz (target %g, detected %g)", hz, out, target, p.getDetectedHz()); CHECK(finite(b),"finite"); }
  { PitchCorrector p; p.prepare(sr); p.amount=1; p.root=7; p.scale=PitchCorrector::Scale::minor; juce::AudioBuffer<float> b(1,96000); float hz=440*std::pow(2.0f,(68-69)/12.0f)*1.01f; sine(b,sr,hz,0.5f); // G#4 -> G minor has G, A: nearest in scale
    runBlocks([&](float* const* ch,int n,int k){p.process(ch,n,k);},b); float out=pitchOf(b,sr,48000); printf("     G# into G minor -> %g Hz (G=392, A=440)\n", out); CHECK(std::abs(out-392.0f)<4 || std::abs(out-440.0f)<4, "to a scale note %g", out); }
  { PitchCorrector p; p.prepare(sr); juce::AudioBuffer<float> b(1,48000); sine(b,sr,440,0.5f); juce::AudioBuffer<float> orig(b); runBlocks([&](float* const* ch,int n,int k){p.process(ch,n,k);},b); int L=p.getLatencySamples(); float err=0; for(int i=24000;i<48000;++i) err=std::max(err,std::abs(b.getSample(0,i)-orig.getSample(0,i-L))); CHECK(err<0.01f, "in tune passes dry (delayed by the latency) %g", err); }
  { // A real voice drifts: the grain changes often, and must never click
    PitchCorrector p; p.prepare(sr); p.amount=1; p.speedMs=20; juce::AudioBuffer<float> b(1,96000); double ph=0;
    for(int i=0;i<96000;++i){ double cents = 30 + 40*std::sin(2*M_PI*5.5*i/sr); double f = 220*std::pow(2.0,cents/1200); ph += 2*M_PI*f/sr; b.setSample(0,i,0.5f*(float)std::sin(ph)); }
    juce::AudioBuffer<float> orig(b); runBlocks([&](float* const* ch,int n,int k){p.process(ch,n,k);},b,64);
    auto d2 = [](juce::AudioBuffer<float>& x){ float m=0; for(int i=2+48000;i<x.getNumSamples();++i) m=std::max(m,std::abs(x.getSample(0,i)-2*x.getSample(0,i-1)+x.getSample(0,i-2))); return m; };
    float ratio = d2(b)/d2(orig); CHECK(ratio < 6.0f, "vibrato (was up to 460x before grains changed only at tap jumps): second difference %gx the input's", ratio); }
  { // CPU: the worst 64-sample block must be far below its real-time budget
    PitchCorrector p; p.prepare(sr); juce::AudioBuffer<float> b(1,96000); sine(b,sr,200,0.5f); double worst=0;
    for(int s=0;s<b.getNumSamples();s+=64){ float* ch[1]={b.getWritePointer(0)+s}; auto t0=juce::Time::getHighResolutionTicks(); p.process(ch,1,64); worst=std::max(worst, juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-t0)); }
    printf("     worst block %.3f ms of %.3f ms\n", worst*1000, 64/sr*1000); CHECK(worst < 64/sr*0.25, "no spikes"); }
  for (float hz : { 40.0f, 150.0f, 600.0f, 2500.0f, 9000.0f })
  { // The three bands add back up to the input's level (an all-pass), at the crossovers too
    Crossover3 x; x.set(sr, 150, 2500); x.reset(); juce::AudioBuffer<float> b(1,48000); sine(b,sr,hz,0.5f);
    for(int i=0;i<b.getNumSamples();++i){ float lo,mi,hi; x.split(b.getSample(0,i),0,lo,mi,hi); b.setSample(0,i,lo+mi+hi); }
    float db = gainToDb(peakOf(b,24000)/0.5f); CHECK(std::abs(db) < 0.1f, "crossover flat at %g Hz: %+.3f dB", hz, db); CHECK(finite(b),"finite"); }
  { // The master limiter's ceiling minus its true-peak margin holds on loud noise
    Limiter lim; lim.prepare(sr,512); lim.releaseMs=150; lim.ceilingDb=-1.5f; lim.inputDb=10; juce::Random r(7); juce::AudioBuffer<float> b(2,96000);
    for(int c=0;c<2;++c) for(int i=0;i<b.getNumSamples();++i) b.setSample(c,i,(r.nextFloat()*2-1)*0.7f);
    runBlocks([&](float* const* ch,int n,int k){lim.process(ch,n,k);},b); CHECK(peakOf(b) <= dbToGain(-1.5f)+1e-5f, "master ceiling held %g", peakOf(b)); }
  printf("%d failures\n", fails); return fails;
}
