#include "CSsb.h"

CSsb::CSsb(QObject *parent,Mode mode) :
    CDemodulatorBase(parent)
{
}

void CSsb::doWork() {

}

QString CSsb::getName() {
    return QString("CSsb");
}
