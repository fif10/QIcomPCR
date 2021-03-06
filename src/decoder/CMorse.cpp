/**********************************************************************************************
  Copyright (C) 2012 Fabrice Crohas <fcrohas@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

**********************************************************************************************/
#include "CMorse.h"
#include <QDebug>

//#define GOERTZEL
//#define KALMAN

CMorse::CMorse(QObject *parent, uint channel) :
    IDecoder(parent)
  ,frequency(3050) // Just for Hello sample
  ,acclow(0)
  ,accup(0)
  ,marks(0)
  ,spaces(0)
  ,markdash(0)
  ,word("")
  ,agclimit(2)
  ,bandwidth(250)
  ,Pp(0.0)
  ,Q(0.022)
  ,R(0.617)
  ,lastestimation(0.0)

{
    // On creation allocate all buffer at maximum decoder size
    mark_i = new double[getBufferSize()];
    mark_q = new double[getBufferSize()];
    xval = new double[getBufferSize()];
    yval = new double[getBufferSize()];
    corr = new double[getBufferSize()];
    mark = new double[getBufferSize()];
    space = new double[getBufferSize()];
    avgcorr = new double[getBufferSize()];
    audioData[0] = new double[getBufferSize()];
    audioBuffer[0] = new double[getBufferSize()];

    // Calculate correlation length
    correlationLength = 50; //SAMPLERATE/201; // Default creation is 6Khz

    double freq = 0.0;
    // Generate Correlation for this frequency
    for (int i=0; i< correlationLength;i++) {
        mark_i[i] = cos(freq);
        mark_q[i] = sin(freq);
        freq += 2.0*M_PI*frequency/SAMPLERATE;
    }

    //Init hash table with morse code
    for (int i=0; i < sizeof(cw_table)/sizeof(CW_TABLE)-1; i++) {
        //qDebug() << "init hash table with " << QString(cw_table[i].chr) << " symbol " << QString(cw_table[i].rpr);
        code.insert(QString(cw_table[i].rpr),cw_table[i].chr);
    }
    // Save local selected channel
    this->channel = channel;
#ifdef FIR
    winfunc = new CWindowFunc(this);
    winfunc->init(64);
    winfunc->hamming();
    //int order = winfunc->kaiser(40,frequency,bandwidth,SAMPLERATE);
    // band pass filter
    fbandpass = new CFIR<double>();
    fbandpass->setOrder(64);
    fbandpass->setWindow(winfunc->getWindow());
    // arbitrary order for 200 Hz bandwidth
    fbandpass->setSampleRate(SAMPLERATE);
    fbandpass->bandpass(frequency,bandwidth);
#else
    fbandpass = new CIIR(this);
    fbandpass->setOrder(2); // second order
    fbandpass->setSampleRate(SAMPLERATE);
    //fbandpass->highpass(frequency);
    fbandpass->bandpass(frequency,bandwidth);
#endif
    memset(yval,0,getBufferSize()*sizeof(double));
}

CMorse::~CMorse()
{
    delete [] mark_i;
    delete [] mark_q;
    delete [] xval;
    delete [] yval;
    delete [] corr;
    delete [] mark;
    delete [] space;
    delete [] avgcorr;
    delete [] audioData[0];
    delete [] audioBuffer[0];
    delete fbandpass;
}

void CMorse::decode(int16_t *buffer, int size, int offset)
{
    // Convert to double and normalize between -1.0 and 1.0
    double peak = 0.0;
    for (int i=0 ; i < size; i++) {
        audioData[0][i] = buffer[i]*1.0/32768.0;
        xval[i] = i;
    }
    // Pass band filter at frequency
    // With width of 200 Hz
    fbandpass->apply(audioData[0], getBufferSize());
#if 0
    emit dumpData(xval,audioData[0],getBufferSize());
    return;
#else
#if 1
    // Correlation of with selected frequency
    for(int i=size-1; i >= 0; i--) { //
        // Init correlation value
        corr[i] = 0.0;
#ifdef GOERTZEL
        // Correlate en shift over correlation length
        corr[i]  = sqrt(pow(goertzel(&audioData[0][i],correlationLength, frequency , SAMPLERATE),2));
#else

        for (int j=correlationLength-1; j>=0; j--) {
            if (i-j>=0) {
                corr[i] += sqrt(pow(audioData[0][i-j] * mark_i[j],2) + pow(audioData[0][i-j] * mark_q[j],2));
            } else
                corr[i] += sqrt(pow(audioBuffer[0][size-i-j] * mark_i[j],2) + pow(audioBuffer[0][size-i-j] * mark_q[j],2));
        }
#endif
#ifdef KALMAN1
        // Kalman filter
        double Pt = Pp + Q;
        //calculate the Kalman gain
        K = Pt * (1.0/(Pt + R));
        //correct
        corr[i] = lastestimation + K * (corr[i] - lastestimation);
        P = (1- K) * Pt;
        //update our last's
        Pp = P;
        lastestimation = corr[i];
#endif
        if (corr[i]>peak) peak=corr[i];

        // Calculate average
        avgcorr[i] = ((corr[i] / 2.0) > agclimit) ? corr[i] / 2.0 : agclimit ;

        // Moving Average filter result of correlation
        yval[i] = corr[i];
        if ((i+10)<size) {
            for (int v=1; v < 10; v++) {
                    yval[i] += yval[i+v]; // this is max value after correlation
            }
            yval[i] = yval[i] / 10;
        } else
            yval[i] = corr[i];
        xval[i] = i;

    }
    // Copy buffer back for next iteration
    memcpy(audioBuffer[0],audioData[0], size*sizeof(double));
#endif
    // Low pass filter for orig CW signal
#ifdef GOERTZEL2
    audioData[0] = yval;
    flowpass->process(getBufferSize(), audioData);
    yval = audioData[0];
#endif
    // Do low pass filtering
    //flowpass->apply(yval, getBufferSize());
    // Now calculation of timing
#if 1
    agc = agclimit; //peak / 2.0; // average value per buffer size
    if (agc<agclimit) agc=agclimit; // minimum detection signal is 1.0
    // Detect High <-> low state and timing
    for (int i=0; i<size; i++) {
        // if > then it is mark
        if (yval[i] >agc) {
            // Start to coutn time in sample
            if (acclow > 0) {
                double time = acclow * 1000.0 /22050.0;
                if (time > 5) {// only accept if impulse is > 5ms
                    //qDebug() << QString("space state for %1 ms").arg(time); // print in millisecond result
                    acclow = 0;
                    space[spaces] = time;
                    // try to calculate new symbol
                    CheckLetterOrWord(spaces, marks);
                    //translate(edge); // Try to determine symbol of this position
                    spaces++;
                    accup++;
                } else { acclow=0; accup++; }
            } else {
                // Try to find new agc value
                //if (yval[i] > peak)
                //peak = yval[i];
                accup++;
            }
        }
        // if we were in high state then it is end, so dump result
        if (yval[i] < agc)  {
            // How much milliseconds ?
            if (accup >0 ) {
                double time = accup * 1000.0 /22050.0;
                if (time > 5) {// only accept if impulse is > 5ms
                    //qDebug() << QString("mark state for %1 ms").arg(time); // print in millisecond result
                    // reset acc
                    accup = 0;
                    mark[marks] = time;
                    // try to calculate new symbol only using high state
                    translate(marks,spaces); // Try to determine symbol of this position
                    //qDebug() << "symbols = " << symbols;
                    marks++;
                    acclow++;
                    // Register new agc value now
                    //agc = peak / 2.0;
                    //qDebug() << "agc value is " << agc;
                    //if (agc<agclimit) agc=agclimit; // minimum detection signal is 1.0
                } else  { accup=0; acclow++; }
            } else
                acclow++;
        }
    }
    // Copy back last timing
    //todo
    // A high state is still running
    if (accup > 0) {
        // so copy space at first position for next buffer
        space[0] = space[spaces-1];
        spaces = 1;
    }
    if (acclow > 0) {
        mark[0] = mark[marks-1];
        marks = 1;
    }
#endif
    // Send correlated signal to scope
    emit dumpData(xval,yval,getBufferSize());
    //qDebug() << "Kalman Q=" << Q << " R=" << R << " T="<< -1.0*log(Q/R);
#endif
}

uint CMorse::getDataSize()
{
    // wanted buffer size per sample
    // 16 bit for this one
    return 16;
}

uint CMorse::getChannel()
{
    // channel used by this demodulator
    return channel;
}

uint CMorse::getBufferSize()
{
    // Buffer size
    return 4096;
}

void CMorse::slotFrequency(double value)
{
    // Calculate frequency value from selected FFT bin
    // only half samplerate is available and FFT is set to 128 per channel
    frequency = value; // value * SAMPLERATE / 512.0; // SAMPLERATE / 512 and displaying graph is 0 to 128
    // New correlation length as frequency selected has changed

    //correlationLength = 50;
    markdash = 0.0;
    // Generate Correlation for this frequency
    GenerateCorrelation(correlationLength);
    qDebug() << "Correlation generated for frequency " << frequency << " hz";
    // Update bandpass filter frequency
    fbandpass->bandpass(frequency,bandwidth);
    //fbandpass->highpass(frequency);
}

void CMorse::translate(int position, int position2)
{
    // start only if we have two edge
    if (position > 0) {
        // Compute ratio between
        // high state timing
        double ratio = mark[position] / mark[position-1];
        //qDebug() << "ratio " << ratio;
        if (ratio>2.0) // the two last symbol sequence are -.
        {
            // This is a dash
            // Save the timming as referece
            //if (mark[position]> markdash)
                markdash = mark[position];
            //calculate new correlation length
            //GenerateCorrelation(markdash);
            // if already a symbol is here and is a jocker
            if ((symbols.length()>0)) // && (symbols.at(symbols.length()-1)) == QChar('*'))
            {
                // replace it
                symbols[symbols.length()-1] = QChar('.');
                symbols += QChar('-');
            }
            else
                symbols += QChar('-');
        }
        else if (ratio < 0.50) // the two last symbol sequence are .-
        {
            // if already a symbol is here and is a jocker
            if ((symbols.length()>0)) // && (symbols.at(symbols.length()-1)) == QChar('*'))
            {
                // replace it
                markdash = mark[position-1];
                symbols[symbols.length()-1] = QChar('-');
                symbols += QChar('.');
            }
            else {
                symbols += QChar('.');
            }
        }
        else // no one match then symbol is same as previous
        {
            if (symbols.length()>0) // same as previous symbol
                symbols += QChar(symbols.at(symbols.length()-1));
            else // set a jocker
            {
                // This case is when ratio is 1.0 and symbol is empty
                // use space / mark ratio to know if it is a dash or a point
//                double symRatio = 0.0;
//                if (position2>0) {
//                    symRatio = mark[position]/space[position2-1]; // we are in mark so space can only be before
//                    //qDebug() << "case mark " << mark[position] << " space " << space[position2-1] <<  " symratio " << symRatio;
//                    if (symRatio > 2.5)
//                        symbols += QChar('-');
//                    else if (symRatio < 0.85)
//                    {
//                        symbols += QChar('.');
//                    } else
//                    {
                        // Compare is still around 1.0
                        // So check with markdash timing
                        if (markdash > 0.0)
                        {
                            double symRatio = markdash / mark[position];
                            if (symRatio > 2.0)
                                symbols += QChar('.');
                            else
                                symbols += QChar('-');
                        } else
                            symbols += QChar('*');
//                    }
//                }
//                else
//                    symbols += QChar('*');
            }
        }

    } else symbols += QChar('.');
}

void CMorse::CheckLetterOrWord(int position, int position2)
{
    if (position > 0) {
        // Compute ratio between
        // high state timing
        double ratiosym  = 0.0;
        double ratio = space[position] / space[position-1];
        if (position2>0)
            ratiosym = space[position] / mark[position2-1];
        // Special case when dash symbol is alone
        // Check ratio
        if (markdash> 0.0) // we know dash timming here
        {
            double dashcmp = markdash/space[position];
            if (( dashcmp < 2.0) && (dashcmp>0.50))
            {
                //qDebug() << "check ratio mark/space " << markdash/space[position];
                // This is a space
                //emit sendData(QString("space detected"));
                ratio = 3.0;
            }
        }
        //emit sendData(QString("symbols %2").arg(symbols));
        //qDebug() << "Check ratio space " << ratio << " ratiosym " << ratiosym << " word is " << word;
        if (((ratio > 2.0) && (ratio < 4.0)) || ((ratiosym > 2.0) && (ratiosym < 4.0))) // || ((symbols.at(symbols.length()-1)=='-') && ((ratiosym < 2.0) && (ratiosym>0.85)))) // a space between letter is detected
        {
            if (code.contains(symbols)) {
                word += code.value(symbols);
                emit sendData(QString(code.value(symbols)));
                //emit sendData(QString("Decoded char is %1 with symbols %2").arg(QChar(code.value(symbols))).arg(symbols));
            }
            else {
                emit sendData(QString("*"));
                //emit sendData(QString("Unknown char %1").arg(symbols));
            }
            symbols = QString("");
        }
        if ((ratio>4.0) || (ratiosym > 4.0))
        {
            if (code.contains(symbols)) {
                word += code.value(symbols);
                word += " ";
                //emit sendData(word);
                emit sendData(QString("%1 ").arg(QString(code.value(symbols))));
                word = "";
                //emit sendData(QString("Decoded char is %1 with symbols %2").arg(QChar(code.value(symbols))).arg(symbols));
            }
            else {
                word += " ";
                emit sendData(QString(" "));
                //emit sendData(word);
                word = "";
                //emit sendData(QString("Unknown char %1").arg(symbols));
            }
            //emit sendData(QString("Space detected"));
            symbols = QString("");
        }
    }


}

void CMorse::GenerateCorrelation(int length)
{
    double freq = 0.0;
    correlationLength = length;
    for (int i=0; i< correlationLength;i++) {
        mark_i[i] = cos(freq);
        mark_q[i] = sin(freq);
        freq += 2.0*M_PI*frequency/SAMPLERATE;
    }

}

void CMorse::setThreshold(double value)
{
    agclimit = value * 1.0;
    qDebug() << " threshold=" << agclimit;
    //emit sendData(QString("Adjust threshold %1").arg(agclimit));
}

void CMorse::setCorrelationLength(int value)
{
    GenerateCorrelation(value);
    //emit sendData(QString("Adjust correlation %1").arg(value));
    //frequency = frequency + value;
    //qDebug() << "Fine tunning frequency " << frequency;
    //GenerateCorrelation(50);
}

void CMorse::slotBandwidth(double value)
{
    bandwidth = value; // * SAMPLERATE / 512.0;
    // Calculate new filter order
    // approx is 4/normalized bandwidth
    double bn = 2*M_PI*bandwidth/SAMPLERATE;
#ifdef FIR
    // New order is
    int order = 4 / bn;
    qDebug() << "order is " << order << " for bandwidth " << bn << " normalized order even is " << (order % 2);
    // it must be even
    //order = winfunc->kaiser(40,frequency,bandwidth,SAMPLERATE);
    if (order % 2 > 0) order += 1;
    // Update windows func length
    winfunc->init(order+1);
    winfunc->hamming();
    // Update bandpass params
    fbandpass->setOrder(order);
    fbandpass->setWindow(winfunc->getWindow());
#endif
    fbandpass->bandpass(frequency,bandwidth);
    //fbandpass->highpass(frequency);
}

double CMorse::goertzel(double *x, int N, double freq, int samplerate)
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
