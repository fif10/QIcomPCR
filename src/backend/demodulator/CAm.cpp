#include "CAm.h"

CAm::CAm(QObject *parent, Mode mode) :
    IDemodulator(parent)
{
    // Build Bandpass filter
    winfunc = new CWindowFunc(this);
    winfunc->init(64);
    winfunc->hamming();
    // Set FIR filter
    filter = new CFIR<int16_t>();
    filter->setOrder(64);
    filter->setWindow(winfunc->getWindow());
    filter->setSampleRate(SAMPLERATE);
    filter->lowpass(11000);

}

void CAm::doWork() {
    update = true;
    int i, pcm;
    // First downsample
    len = IDemodulator::downsample(buffer,len);
    // Do demodulation
    for (i = 0; i < len; i += 2) {
        // hypot uses floats but won't overflow
        //r[i/2] = (int16_t)hypot(lp[i], lp[i+1]);
        pcm = buffer[i] * buffer[i];
        pcm += buffer[i+1] * buffer[i+1];
        // Output stereo buffer only
        buffer[i] = (int)sqrt(pcm) * 1;
        buffer[i+1] = (int)sqrt(pcm) * 1;
    }
    // Apply audio filter
    len = IDemodulator::resample(buffer,len,filterfreq);
    IDemodulator::processSound(buffer,len);
    update = false;
}

