#include "ISound.h"

ISound::ISound(QObject *parent) :
    QThread(parent)
{
}

void ISound::SetDemodulator(CDemodulator *value)
{
    demod = value;
}

CDemodulator *ISound::GetDemodulator()
{
    return demod;
}

void ISound::DecodeBuffer(int16_t *buffer, int size)
{
    demod->slotDataBuffer(buffer, size);
}

void ISound::setRunning(bool value)
{

}

QHash<QString, int> ISound::getDeviceList()
{

}

void ISound::selectInputDevice(QString device)
{

}

void ISound::selectOutputDevice(QString device)
{

}
