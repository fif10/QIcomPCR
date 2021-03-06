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
#include "CSoundFile.h"

CSoundFile::CSoundFile(QObject *parent) :
    ISound(parent),
    samplerate(0),
    channels(0),
    frames(0),
    running(false),
    loop(false),
    blankCount(0),
    timing(23)
#ifdef WITH_SAMPLERATE
    ,converter(NULL)
#endif

{
}

CSoundFile::~CSoundFile()
{
#ifdef WITH_SAMPLERATE
    if (converter != NULL)
        src_delete(converter);
    if (inputbufferf)
        delete [] inputbufferf;
    if (outputbufferf)
        delete [] outputbufferf;
#endif
    if (inputbuffer)
        delete [] inputbuffer;
}

bool CSoundFile::Load(QString &fileName)
{
    bool loaded = false;

    // Sound file info struct
    SF_INFO sfinfo;
    memset(&sfinfo, 0, sizeof(SF_INFO));

    //Load file
    pFile = sf_open(fileName.toLocal8Bit().data(), SFM_READ, &sfinfo);
    if (pFile != NULL)
    {
        channels = sfinfo.channels;
        samplerate = sfinfo.samplerate;
        frames = sfinfo.frames;
        double oversample_factor = SAMPLERATE*1.0 / samplerate*1.0;
        qDebug() << " Information channels count is " << channels << " samplerate is " << samplerate << " frames count is " << frames << " oversample ratio " << ceil(oversample_factor);
        // Init sample rate converter
        int error = 0;
        converter =  src_new( SRC_SINC_BEST_QUALITY , channels, &error);
        if (error != 0) {
            qDebug() << " Error creating sample rate converter : " << src_strerror(error);
        }
        double sampletiming = 1.0 / SAMPLERATE;
        timing = (BUFFER_SIZE / channels) * sampletiming *1000; // millisecond of a buffer size per channel
        qDebug() << " buffer time is " << timing;
        /* we can only cope with integer submultiples */
        // Init buffer size to read
        inputbuffer = new int16_t[BUFFER_SIZE];
        memset(inputbuffer,0,BUFFER_SIZE);
#ifdef WITH_SAMPLERATE
        inputbufferf = new float[BUFFER_SIZE];
        outputbufferf = new float[BUFFER_SIZE];
        // Fill samplerate structure
        dataconv.data_in = inputbufferf;
        dataconv.data_out = outputbufferf;
        dataconv.src_ratio = oversample_factor;
        if (channels == 1) {
            dataconv.input_frames = BUFFER_SIZE/floor(oversample_factor);
            dataconv.output_frames = BUFFER_SIZE;
        } else {
            dataconv.input_frames = FRAME_SIZE/floor(oversample_factor);
            dataconv.output_frames = FRAME_SIZE;
        }
        dataconv.end_of_input = 0;
#endif
        loaded=true;
    } else
            loaded = false;

    return loaded;
}

bool CSoundFile::Read(int16_t *data, int offset)
{
    sf_count_t c;
    c = sf_readf_short(pFile, inputbuffer, BUFFER_SIZE/channels);
    if (c!=BUFFER_SIZE/channels) {
        /* rewind */
        sf_seek(pFile, 0, SEEK_SET);
        // At end of file send an empty buffer before next
        c = sf_readf_short(pFile, inputbuffer, BUFFER_SIZE/channels);
        // End of read
        loop = true;
    }
    if(channels==2)
    {
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            data[i] = inputbuffer[i];
            data[i+1] = inputbuffer[i+1];
        }
    }
    else
    {
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            data[i] = inputbuffer[i];
            data[i+1] = inputbuffer[i];
        }
    }

    this->DecodeBuffer(data,BUFFER_SIZE);
    // add data to ringbuffer of the encoder
    soundStream->setData(data,BUFFER_SIZE);
    // Start the workker function;
    QMetaObject::invokeMethod(soundStream, "doWork");//, Qt::QueuedConnection);
    msleep(timing); // Give how much millisecond we wait before next salve of BUFFER_SIZE/2 samples per channels
    return true;
}

#ifdef WITH_SAMPLERATE
bool CSoundFile::ReadOverSample(int16_t *data, int offset, double ratio)
{
        sf_count_t c;
        // Read only small part of buffer to do over sampling
        c = sf_readf_float(pFile, inputbufferf, (BUFFER_SIZE/floor(ratio))/channels);
        if (c!=(BUFFER_SIZE/floor(ratio))/channels) {
            /* rewind */
            sf_seek(pFile, 0, SEEK_SET);
            // At end of file send an empty buffer before next
            c = sf_readf_float(pFile, inputbufferf, (BUFFER_SIZE/floor(ratio))/channels);
            // End of read
            loop = true;
        }
        // Calculate oversample conversion
        src_process(converter, &dataconv);
        //qDebug() << "output 1 frames gen is " << dataconv.output_frames_gen << " from input used " << dataconv.input_frames_used;
        src_float_to_short_array(outputbufferf,data,BUFFER_SIZE);
        this->DecodeBuffer(data,BUFFER_SIZE);
        // add data to ringbuffer of the encoder
        soundStream->setData(data,BUFFER_SIZE);
        // Start the workker function;
        QMetaObject::invokeMethod(soundStream, "doWork");
        // sleep in ms is the size of buffer  * samplerate timming per channel
        msleep(timing); // Give how much millisecond we wait before next salve of BUFFER_SIZE/2 samples per channels
        if (dataconv.input_frames_used == 0) {
            // Calculate oversample conversion
            src_process(converter, &dataconv);
            //qDebug() << "output 2 frames gen is " << dataconv.output_frames_gen << " from input used " << dataconv.input_frames_used;
            src_float_to_short_array(outputbufferf,data,BUFFER_SIZE);
            this->DecodeBuffer(data,BUFFER_SIZE);
            // add data to ringbuffer of the encoder
            soundStream->setData(data,BUFFER_SIZE);
            // Start the workker function;
            QMetaObject::invokeMethod(soundStream, "doWork");
            msleep(timing); // Give how much millisecond we wait before next salve of BUFFER_SIZE/2 samples per channels
        }
        return true;
}
#endif

void CSoundFile::DecodeBuffer(int16_t *buffer, int size)
{
    ISound::DecodeBuffer(buffer,size);
}

void CSoundFile::setRunning(bool value)
{
    running = true;
}

void CSoundFile::run()
{
    // Sound streaming
    //qDebug() << QThread::currentThreadId() << " File reading thread";
    soundStream = new CSoundStream();
    audioEncode = new QThread();
    soundStream->moveToThread(audioEncode);
    audioEncode->start();
    // Wait for event to process
    // Allocate data file for decoder
    int16_t *data = new int16_t[BUFFER_SIZE];
    blankCount = 0;
    double oversample_factor = SAMPLERATE / samplerate;

    while(running) {
        // Split reading into chunk of buffer size
        // to simulate sound card reading
        // and better
        int i=0;
        // Read all buffer
        if (oversample_factor == 1.0) {
            Read(data,i);
        } else {
#ifdef WITH_SAMPLERATE
            ReadOverSample( data, i, oversample_factor);
#else
            qDebug() << "Sample rate conversion not supported";
#endif
        }

        // If reach end of buffer, do some blank before next loop
        while (loop) {
            memset(data,0,BUFFER_SIZE);
            blankCount++;
            if (blankCount ==16) {
                loop=false;
                blankCount = 0;
            }
            this->DecodeBuffer(data,BUFFER_SIZE);
            msleep(timing);

        }
    }
    delete [] data;
    audioEncode->terminate();
    delete soundStream;
    delete audioEncode;
    terminate();
}


void CSoundFile::terminate()
{
    if(pFile==NULL)
        return;
    sf_close(pFile);
    pFile = NULL;

}

void CSoundFile::Record(QString &filename, bool start)
{

}

void CSoundFile::setChannel(uint value)
{
    if (soundStream != NULL) {
        soundStream->setChannel(value);
    }
}

void CSoundFile::Play(int16_t *buffer, int size) {

}
