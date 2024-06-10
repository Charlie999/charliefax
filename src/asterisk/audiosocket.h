#ifndef AUDIOSOCKET_H
#define AUDIOSOCKET_H

#define AUDIOSOCKET_TYPE_TERMINATE 0x00
#define AUDIOSOCKET_TYPE_UUID 0x01
#define AUDIOSOCKET_TYPE_AUDIO 0x10
#define AUDIOSOCKET_TYPE_ERROR 0xff

struct asterisk_audiosock_hdr {
    unsigned char type;
    unsigned short len;
} __attribute__((packed));

double samp_fir[] =  {
    0.000000000000000000,
    -0.000043508169090981,
    -0.000226216376191331,
    -0.000404819475403031,
    -0.000120857436900967,
    0.001129355306997293,
    0.003278511263483086,
    0.005069280449805869,
    0.004104864281927150,
    -0.001880516278496444,
    -0.012759161533310302,
    -0.024047300321494044,
    -0.027030569255047501,
    -0.011983084908160189,
    0.026432481447379391,
    0.084396709363990463,
    0.148020704018249905,
    0.197766179775875017,
    0.216595895692772961,
    0.197766179775875017,
    0.148020704018249905,
    0.084396709363990477,
    0.026432481447379402,
    -0.011983084908160193,
    -0.027030569255047515,
    -0.024047300321494044,
    -0.012759161533310302,
    -0.001880516278496445,
    0.004104864281927152,
    0.005069280449805868,
    0.003278511263483093,
    0.001129355306997293,
    -0.000120857436900967,
    -0.000404819475403031,
    -0.000226216376191331,
    -0.000043508169090981,
    0.000000000000000000
};
#define FIR_TAPS (sizeof(samp_fir)/sizeof(*samp_fir))

#endif