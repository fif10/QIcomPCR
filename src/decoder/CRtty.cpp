#include "CRtty.h"

CRtty::CRtty(QObject *parent, uint channel) :
    IDemodulator(parent),
    channel(0),
    frequency(4500),
    correlationLength(40),
    bandwidth(250),
    freqlow(1600.0),
    freqhigh(4000.0),
    baudrate(50),
    inverse(1.0),
    letter(""),
    bitcount(0),
    started(false),
    mark_q(NULL),
    mark_i(NULL),
    space_i(NULL),
    space_q(NULL)
{
    // Table init
    xval = new double[getBufferSize()];
    yval = new double[getBufferSize()];
    corrmark = new double[getBufferSize()];
    corrspace = new double[getBufferSize()];
    avgcorr = new double[getBufferSize()];
    audioData[0] = new double[getBufferSize()];
    audioData[1] = new double[getBufferSize()];

    // Save local selected channel
    this->channel = channel;

    // Build filter
    winfunc = new CWindowFunc(this);
    winfunc->init(65);
    winfunc->hann();
    //int order = winfunc->kaiser(40,frequency,bandwidth,SAMPLERATE);
    // band pass filter
    fmark = new CFIR(this);
    fspace = new CFIR(this);
    flow = new CFIR(this);
    fmark->setWindow(winfunc->getWindow());
    fspace->setWindow(winfunc->getWindow());
    // arbitrary order for 200 Hz bandwidth
    // for mark frequency
    fmark->setOrder(64);
    fmark->setSampleRate(SAMPLERATE);
    fmark->bandpass(freqhigh,bandwidth);

    // for space frequency
    fspace->setOrder(64);
    fspace->setSampleRate(SAMPLERATE);
    fspace->bandpass(freqlow,bandwidth);

    // Low pass filter at the end
    flow->setOrder(64);
    flow->setSampleRate(SAMPLERATE);
    flow->lowpass(200.0);
    bit = ((1/baudrate) * SAMPLERATE); // We want it in sample unit
    qDebug() << "bit size for " << baudrate << " baud is " << bit;
    GenerateCorrelation(50);
}

CRtty::~CRtty()
{
    delete [] xval;
    delete [] yval;
    delete [] corrmark;
    delete [] corrspace;
    delete [] avgcorr;
    delete [] audioData[0];
    delete [] audioData[1];
    delete [] mark_i;
    delete [] mark_q;
    delete [] space_i;
    delete [] space_q;
    delete fmark;
    delete fspace;
}

void CRtty::decode(int16_t *buffer, int size, int offset)
{
    // Convert to double and normalize between -1.0 and 1.0
    double peak = 0.0;
    for (int i=0 ; i < size; i++) {
        audioData[0][i] = buffer[i]*1.0/32768.0;
        audioData[1][i] = buffer[i]*1.0/32768.0;
    }
    // Pass band filter at frequency
    // With width of 200 Hz
    fmark->apply(audioData[0],getBufferSize());
    fspace->apply(audioData[1],getBufferSize());

    // Correlation of with selected frequency
    for(int i=0; i < size-correlationLength; i++) { //
        // Init correlation value
        corrmark[i] = 0.0;
        corrspace[i] = 0.0;
        for (int j=0; j < correlationLength; j++) {
            corrmark[i]  = sqrt(pow(audioData[0][i+j] * mark_i[j],2) + pow(audioData[0][i+j] * mark_q[j],2));
            corrspace[i] = sqrt(pow(audioData[1][i+j] * space_i[j],2) + pow(audioData[1][i+j] * space_q[j],2));
        }
        // Output results
        avgcorr[i] = (corrmark[i] - corrspace[i]) * inverse;
        // Moving Average filter result of correlation
#if 0
        if (i>4) {
            // Moving average filter
            yval[i] = avgcorr[i];
            for (int v=0; v < 4; v++) {
            yval[i] += yval[i-v]; // this is max value after correlation
            }
            yval[i] = yval[i] / 4.0;
        } else
#endif
            // Save to result buffer
            yval[i] = avgcorr[i];

        // Save to result buffer
        xval[i] = i;
    }

    // Fill end of buffer with 0 as the end is not used
    // Maybe suppress this if init is well done
    for (int i=0; i < correlationLength; i++) {
        yval[(size-(int)correlationLength)+i] = 0.0;
        xval[(size-(int)correlationLength)+i] = (size-(int)correlationLength)+i;
    }

    // Apply lowpass filter
    flow->apply(yval,getBufferSize());
    // Decode baudo code
    // One bit timing is 1/baudrate
    accmark  = 0;
    accspace = 0;

    for (int i=0; i< size; i++) {
        if (yval[i]<0.0) {
            // Start measure low state to detect start bit
            if (accmark>0) { // is this a stop bit ?
                if ((abs(accmark - (bit * STOPBITS)) <50.0)) // allow a margin of 50 samples
                {
                    qDebug() << "stop bit detected with letter " << letter;
                    // emit letter
                    letter.append("\r\n");
                    letter.append("Stop bits\r\n");
                    emit sendData(letter);
                    letter = ""; //reset letter
                    bitcount = 0;
                    started = false;
                } // not a stop bit so
                else {
                    // how much bit at 1 ?
                    int count = ceil((double)(accmark / bit));
                    bitcount += count; // Cut into number of bit
                    qDebug() << "bits  mark count "<< bitcount << "ceil("<< accmark <<" / bit)=" << count;
                    letter.append(QString(" %1M").arg(count));
                }
                accmark = 0;
            }
            else {
                accspace += 1;
            }
        }
        if (yval[i]>0.0) {
            // this is a high state now
            if (accspace >0) { // do we come from low state ?
                // is this a start bit ?
                if (started)
                {
                    // how much bit at 0 ?
                    int count = ceil((double)(accspace / bit));
                    bitcount += count; // Cut into number of bit
                    qDebug() << "bits  space count "<< bitcount << "ceil("<< accspace <<" / bit)=" << count;
                    letter.append(QString(" %1S").arg(count));
                }

                if ((abs(accspace - (bit*STARTBITS)) <50.0) && (!started)) // allow a margin of 50 samples
                {
                    qDebug() << "start bit detected";
                    emit sendData(QString("Start bit\r\n"));
                    // Init bit counter
                    bitcount = 0;
                    letter = ""; //reset letter
                    started = true;
                }
                accspace = 0;
            }
            else {
                accmark +=1;
            }
        }
    }
    emit dumpData(xval, yval, size);
}

uint CRtty::getDataSize()
{
    return 16;
}

uint CRtty::getBufferSize()
{
    return 4096;
}


uint CRtty::getChannel()
{
    return channel;
}

void CRtty::setThreshold(int value)
{

}

void CRtty::slotBandwidth(double value)
{
    bandwidth = value * SAMPLERATE / 512.0;
    // In rtty it is 400 Hz wide so
    freqlow = frequency - bandwidth/2;
    freqhigh = frequency + bandwidth/2;
}


void CRtty::slotFrequency(double value)
{
    frequency = value * SAMPLERATE / 512; // SAMPLERATE / 512 and displaying graph is 0 to 128
}

void CRtty::setCorrelationLength(int value)
{
    correlationLength = value;
}

void CRtty::GenerateCorrelation(int length)
{
    if (mark_q == NULL) {
        mark_q = new double[length];
    }
    if (mark_i == NULL) {
        mark_i = new double[length];
    }
    if (space_q == NULL) {
        space_q = new double[length];
    }
    if (space_i == NULL) {
        space_i = new double[length];
    }
    double freq1 = 0.0;
    double freq2 = 0.0;
    correlationLength = length;
    for (int i=0; i< correlationLength;i++) {
        mark_i[i] = cos(freq1);
        mark_q[i] = sin(freq1);
        freq1 += 2.0*M_PI*freqlow/SAMPLERATE;
        space_i[i] = cos(freq2);
        space_q[i] = sin(freq2);
        freq2 += 2.0*M_PI*freqhigh/SAMPLERATE;
    }

}

double CRtty::goertzel(double *x, int N, double freq, int samplerate)
{
    double Skn, Skn1, Skn2;
    Skn = Skn1 = Skn2 = 0;

    for (int i=0; i<N; i++) {
        Skn2 = Skn1;
        Skn1 = Skn;
        Skn = 2*cos(2*M_PI*freq/samplerate)*Skn1 - Skn2 + x[i];
    }

    double WNk = exp(-2*M_PI*freq/samplerate); // this one ignores complex stuff
    //float WNk = exp(-2*j*PI*k/N);
    return (Skn - WNk*Skn1);
}
