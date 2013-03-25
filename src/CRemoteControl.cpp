#include "CRemoteControl.h"

#include <iostream>

CRemoteControl::CRemoteControl(QObject* parent): QObject(parent)
{
  connect(&server, SIGNAL(newConnection()), this, SLOT(acceptConnection()));

  server.listen(QHostAddress::Any, 8888);
  connected = false;
}

CRemoteControl::~CRemoteControl()
{
  server.close();
}

void CRemoteControl::acceptConnection()
{
  client = server.nextPendingConnection();

  connect(client, SIGNAL(readyRead()), this, SLOT(startRead()));
  connected = true;
}

void CRemoteControl::startRead()
{
  char buffer[1024] = {0};
  client->read(buffer, client->bytesAvailable());
  decode(buffer);
}

void CRemoteControl::decode(char *buffer)
{
    QString decodedString(buffer);

    if (decodedString.startsWith("INIT")) {
        emit sigInitialize();
    } else
    if (decodedString.startsWith("PWRON")) {
        emit sigInitialize();
        //emit sigPower(true);
    } else
    if (decodedString.startsWith("PWROFF")) {
        emit sigPower(false);
    } else
    if (decodedString.startsWith("AGCON")) {
        emit sigAutomaticGainControl(true);
    } else
    if (decodedString.startsWith("AGCOFF")) {
        emit sigAutomaticGainControl(false);
    } else
    if (decodedString.startsWith("VSCON")) {
        emit sigVoiceControl(true);
    } else
    if (decodedString.startsWith("VSCOFF")) {
        emit sigVoiceControl(false);
    } else
    if (decodedString.startsWith("FILTER")) {
        QString filter = decodedString.replace("FILTER","");
        if (filter == "230K") emit sigFilter(CCommand::e230k);
        if (filter == "50K")  emit sigFilter(CCommand::e50k);
        if (filter == "15K")  emit sigFilter(CCommand::e15k);
        if (filter == "6K")   emit sigFilter(CCommand::e6k);
        if (filter == "28K")  emit sigFilter(CCommand::e28k);
    } else
    if (decodedString.startsWith("FREQ")) {
        QString frequency = decodedString.replace("FREQ","");
        qDebug() << "frequency remote " << frequency.toUInt();
        emit sigFrequency(frequency.toUInt());
    } else
    if (decodedString.startsWith("IF")) {
        emit sigIFShift(128);
    } else
    if (decodedString.startsWith("MOD")) {
        QString modulation = decodedString.replace("MOD","");
        if (modulation == "WFM") emit sigModulation(CCommand::eWFM);
        if (modulation == "FM")  emit sigModulation(CCommand::eFM);
        if (modulation == "AM")  emit sigModulation(CCommand::eAM);
        if (modulation == "LSB") emit sigModulation(CCommand::eLSB);
        if (modulation == "USB") emit sigModulation(CCommand::eUSB);
        if (modulation == "CW")  emit sigModulation(CCommand::eCW);
    } else
    if (decodedString.startsWith("NBON")) {
        emit sigNoiseBlanker(true);
    } else
    if (decodedString.startsWith("NBOFF")) {
        emit sigNoiseBlanker(true);
    } else
    {
        client->write("UNK");
        return;
    }

    client->write("ACK");

}

void CRemoteControl::sendData(QString value)
{
    if (connected)
        client->write(value.toLocal8Bit());
}