#include "CAcarsGPL.h"
#include <math.h>
#include <QDebug>

CAcarsGPL::CAcarsGPL(QObject *parent, uint channel) :
    IDemodulator(parent)
{
    init_bits();
    init_mesg();
    this->channel = channel;
}

void CAcarsGPL::init_bits(void)
{
    int i;
    for (i = 0; i < BITLEN; i++)
        h[i] = sin(2.0 * M_PI * (float) i / (float) BITLEN);

    for (i = 0; i < BITLEN; i++) {
        bstat[0].hsample[i] = bstat[0].lsample[i] =
            bstat[0].isample[i] = bstat[0].qsample[i] =
            bstat[0].csample[i] = 0.0;
        bstat[1].hsample[i] = bstat[1].lsample[i] =
            bstat[1].isample[i] = bstat[1].qsample[i] =
            bstat[1].csample[i] = 0.0;
    }
    bstat[0].is = bstat[0].clock = bstat[0].sgI = bstat[0].sgQ = 0;
    bstat[1].is = bstat[1].clock = bstat[1].sgI = bstat[1].sgQ = 0;
    bstat[0].phih = bstat[0].phil = bstat[0].dfh = bstat[0].dfl =
        bstat[0].pC = bstat[0].ppC = bstat[0].ea = 0.0;
    bstat[1].phih = bstat[1].phil = bstat[1].dfh = bstat[1].dfl =
        bstat[1].pC = bstat[1].ppC = bstat[1].ea = 0.0;
    bstat[0].lin=bstat[1].lin=1.0;

}

void CAcarsGPL::resetbits(int ch)
{
    bstat[ch].sgI = bstat[ch].sgQ = 0;
}

int CAcarsGPL::getbit(short sample, unsigned char *outbits, int ch)
{
    int i, bt;
    float in, in2;
    float C;
    float I, Q;
    float oscl, osch;
    struct bstat_s *st;
    bt = 0;
    st = &bstat[ch];

    in = (float) sample;
    st->lin = 0.003 * fabs(in) + 0.997 * st->lin;
    in /= st->lin;
    in2 = in * in;

    st->is--;
    if (st->is < 0)
        st->is = BITLEN - 1;

    /* VFOs */
    st->phih += Freqh - VFOPLL * st->dfh;
    if (st->phih >= 4.0 * M_PI)
        st->phih -= 4.0 * M_PI;
    st->hsample[st->is] = in2 * sin(st->phih);
    for (i = 0, st->dfh = 0.0; i < BITLEN / 2; i++) {
        st->dfh += st->hsample[(st->is + i) % BITLEN];
    }
    osch = cos(st->phih / 2.0);

    st->phil += Freql - VFOPLL * st->dfl;
    if (st->phil >= 4.0 * M_PI)
        st->phil -= 4.0 * M_PI;
    st->lsample[st->is] = in2 * sin(st->phil);
    for (i = 0, st->dfl = 0.0; i < BITLEN / 2; i++) {
        st->dfl += st->lsample[(st->is + i) % BITLEN];
    }
    oscl = cos(st->phil / 2.0);

    /* mix */
    st->isample[st->is] = in * (oscl + osch);
    st->qsample[st->is] = in * (oscl - osch);
    st->csample[st->is] = oscl * osch;


    /* bit clock */
    st->clock++;
    if (st->clock >= BITLEN/4 + st->ea) {
        st->clock = 0;

        /*  clock filter  */
        for (i = 0, C = 0.0; i < BITLEN; i++) {
            C += h[i] * st->csample[(st->is + i) % BITLEN];
        }

        if (st->pC < C && st->pC < st->ppC) {

            /* integrator */
            for (i = 0, Q = 0.0; i < BITLEN; i++) {
                Q += st->qsample[(st->is + i) % BITLEN];
            }

            if (st->sgQ == 0) {
                if (Q < 0)
                    st->sgQ = -1;
                else
                    st->sgQ = 1;
            }

            *outbits =
                ((*outbits) >> 1) | (unsigned
                         char) ((Q * st->sgQ >
                             0) ? 0x80 : 0);
            bt = 1;

            st->ea = -BITPLL * (C - st->ppC);
            if(st->ea > 2.0) st->ea=2.0;
            if(st->ea < -2.0) st->ea=-2.0;
        }
        if (st->pC > C && st->pC > st->ppC) {
            /* integrator */
            for (i = 0, I = 0.0; i < BITLEN; i++) {
                I += st->isample[(st->is + i) % BITLEN];
            }

            if (st->sgI == 0) {
                if (I < 0)
                    st->sgI = -1;
                else
                    st->sgI = 1;
            }

            *outbits =
                ((*outbits) >> 1) | (unsigned
                         char) ((I * st->sgI >
                             0) ? 0x80 : 0);
            bt = 1;

            st->ea = BITPLL * (C - st->ppC);
            if(st->ea > 2.0) st->ea=2.0;
            if(st->ea < -2.0) st->ea=-2.0;
        }
        st->ppC = st->pC;
        st->pC = C;
    }
    return bt;
}

void CAcarsGPL::update_crc(unsigned short *crc, unsigned char ch)
{
    unsigned char v;
    unsigned int i;
    unsigned short flag;

    v = 1;
    for (i = 0; i < 8; i++) {
        flag = (*crc & 0x8000);
        *crc = *crc << 1;

        if (ch & v)
            *crc = *crc + 1;

        if (flag != 0)
            *crc = *crc ^ POLY;

        v = v << 1;
    }
}

int CAcarsGPL::build_mesg(char *txt, int len, msg_t * msg)
{
    int i, k;
    char r;

    /* remove special chars */
    for (i = 0; i < len; i++) {
        r = txt[i];
        if (r < ' ' && r != 0x0d && r != 0x0a)
            r = 0xa4;
        txt[i] = r;
    }
    txt[i] = '\0';

    /* fill msg struct */
    k = 0;
    msg->mode = txt[k];
    k++;

    for (i = 0; i < 7; i++, k++) {
        msg->addr[i] = txt[k];
    }
    msg->addr[7] = '\0';

    /* ACK/NAK */
    msg->ack = txt[k];
    k++;

    msg->label[0] = txt[k];
    k++;
    msg->label[1] = txt[k];
    k++;
    msg->label[2] = '\0';

    msg->bid = txt[k];
    k++;

    k++;

    for (i = 0; i < 4; i++, k++) {
        msg->no[i] = txt[k];
    }
    msg->no[4] = '\0';

    for (i = 0; i < 6; i++, k++) {
        msg->fid[i] = txt[k];
    }
    msg->fid[6] = '\0';

    strcpy(msg->txt, &(txt[k]));

    return 1;
}

void CAcarsGPL::init_mesg(void)
{
    mstat[0].state = mstat[1].state = HEADL;
}

int CAcarsGPL::getmesg(unsigned char r, msg_t * msg, int ch)
{
    struct mstat_s *st;

    st = &(mstat[ch]);

    do {
        switch (st->state) {
        case HEADL:
            if (r == 0xff) {
                st->state = HEADF;
                return 8;
            }
            resetbits(ch);
            return 8;
            break;
        case HEADF:
            if (r != 0xff) {
                int i;
                unsigned char m;

                for (i = 0, m = 1; i < 7; i++, m = m << 1) {
                    if (!(r & m))
                        break;
                }
                if (i < 2) {
                    st->state = HEADL;
                    break;
                }
                st->state = BSYNC1;
                st->ind = 0;
                if (i != 2)
                    return (i - 2);
                break;
            }
            return 6;
        case BSYNC1:
            if (r != 0x80 + '+')
                st->ind++;
            st->state = BSYNC2;
            return 8;
        case BSYNC2:
            if (r != '*')
                st->ind++;
            st->state = SYN1;
            return 8;
        case SYN1:
            if (r != SYN)
                st->ind++;
            st->state = SYN2;
            return 8;
        case SYN2:
            if (r != SYN)
                st->ind++;
            st->state = SOH1;
            return 8;
        case SOH1:
            if (r != SOH)
                st->ind++;
            if (st->ind > 2) {
                st->state = HEADL;
                break;
            }
            st->state = TXT;
            st->ind = 0;
            st->crc = 0;
            return 8;
        case TXT:
            update_crc(&st->crc, r);
            r = r & 0x7f;
            if (r == 0x03 || r == 0x17) {
                st->state = CRC1;
                return 8;
            }
            st->txt[st->ind] = r;
            st->ind++;
            if (st->ind > 243) {
                st->state = HEADL;
                break;
            }
            return 8;
        case CRC1:
            update_crc(&st->crc, r);
            st->state = CRC2;
            return 8;
        case CRC2:
            update_crc(&st->crc, r);
            st->state = END;
            return 8;
        case END:
            st->state = HEADL;
            if (st->crc == 0) {
                build_mesg(st->txt, st->ind, msg);
                return 0;
            }
            return 8;
        }
    } while (1);
}

void CAcarsGPL::print_mesg(msg_t * msg)
{
    time_t t;
    struct tm *tmp;
    char pos[128];

    emit sendData(QString("ACARS mode: %1").arg(msg->mode));
    qDebug() << QString("ACARS mode: %1").arg(msg->mode);
    qDebug() << QString(" Aircraft reg: %1").arg( (int)msg->addr);
    qDebug() << QString("Message label: %1").arg((int)msg->label);
    qDebug() << QString(" Block id: %1").arg(msg->bid);
    qDebug() << QString(" Msg. no: %1").arg((int)msg->no);
    qDebug() << QString("Flight id: %1").arg((int)msg->fid);
    emit sendData(QString("Message content:-\n%1").arg(msg->txt));
    qDebug() << QString("Message content:-\n%1").arg(msg->txt);

    //if (posconv(msg->txt, msg->label, pos)==0)
    //    qDebug() << QString("APRS : Addr:%1 Fid:%2 Lbl:%3 pos:%4\n").arg(msg->addr).arg(msg->fid).arg(msg->label).arg(pos);

}

void CAcarsGPL::decode(int16_t *data, int size, int offset)
{
    int ind, len;
    int el=1, er=0; // only one channel
    int nbitl = 0, nbitr = 0;
    int nrbitl =8,  nrbitr = 8;
    unsigned char rl, rr;
    msg_t msgl, msgr;
    int nbch = 1;

    for (ind = 0; ind < size;) {

        if(el) {
        nbitl += getbit(data[ind], &rl, 0);

        if (nbitl >= nrbitl) {
            nrbitl = getmesg(rl, &msgl, 0);
            nbitl = 0;
            if (nrbitl == 0) {
                print_mesg(&msgl);
                nrbitl = 8;
            }
        }
        }
        ind++;

        if (nbch >= 2) {
            if(er) {
            nbitr += getbit(data[ind], &rr, 1);
            if (nbitr >= nrbitr) {
                nrbitr = getmesg(rr, &msgr, 1);
                nbitr = 0;
                if (nrbitr == 0) {
                    print_mesg(&msgl);
                    nrbitr = 8;
                }
            }
            }
            ind++;
        }
    }
}

uint CAcarsGPL::getChannel()
{
    return channel;
}

uint CAcarsGPL::getDataSize()
{
    return 16;
}

uint CAcarsGPL::getBufferSize()
{
    return 16384;
}