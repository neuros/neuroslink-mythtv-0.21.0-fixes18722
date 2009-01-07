
#include <qmutex.h>
#include <qmap.h>

#include "mythcontext.h"
#include "dtvconfparserhelpers.h"

bool DTVParamHelper::ParseParam(const QString &symbol, int &value,
                                const DTVParamHelperStruct *table)
{
    const DTVParamHelperStruct *p = table;

    while (p->symbol != NULL)
    {
        if (p->symbol == symbol.left(p->symbol.length()))
        {
            //symbol = symbol.mid(p->symbol.length());
            value = p->value;
            return true;
        }
        p++;
    }

    return false;
}

QString DTVParamHelper::toString(const char *strings[], int index,
                                 uint strings_size)
{
    if ((index < 0) || ((uint)index >= strings_size))
    {
        VERBOSE(VB_IMPORTANT,
                "DTVParamHelper::toString() index out of bounds");

        return QString::null;
    }

    return strings[index];
}

static QMutex dtv_tt_canonical_str_lock;
static QMap<int,QString> dtv_tt_canonical_str;
void DTVTunerType::initStr(void)
{
    QMutexLocker locker(&dtv_tt_canonical_str_lock);
    dtv_tt_canonical_str[kTunerTypeQPSK]    = "QPSK";
    dtv_tt_canonical_str[kTunerTypeQAM]     = "QAM";
    dtv_tt_canonical_str[kTunerTypeOFDM]    = "OFDM";
    dtv_tt_canonical_str[kTunerTypeATSC]    = "ATSC";
    dtv_tt_canonical_str[kTunerTypeDVB_S]   = "DVB_S";
    dtv_tt_canonical_str[kTunerTypeDVB_C]   = "DVB_C";
    dtv_tt_canonical_str[kTunerTypeDVB_T]   = "DVB_T";
    dtv_tt_canonical_str[kTunerTypeDVB_S2]  = "DVB_S2";
    dtv_tt_canonical_str[kTunerTypeUnknown] = "UNKNOWN";
}

QString DTVTunerType::toString(int _value)
{
    QMutexLocker locker(&dtv_tt_canonical_str_lock);
    QMap<int,QString>::const_iterator it = dtv_tt_canonical_str.find(_value);
    if (it != dtv_tt_canonical_str.end())
        return *it;
    return dtv_tt_canonical_str[kTunerTypeUnknown];
}

const DTVParamHelperStruct DTVTunerType::parseTable[] =
{
    { "QPSK",    kTunerTypeQPSK    },
    { "QAM",     kTunerTypeQAM     },
    { "OFDM",    kTunerTypeOFDM    },
    { "ATSC",    kTunerTypeATSC    },
    { "DVB_S",   kTunerTypeDVB_S   },
    { "DVB_C",   kTunerTypeDVB_C   },
    { "DVB_T",   kTunerTypeDVB_T   },
    { "DVB_S2",  kTunerTypeDVB_S2  },
    { "UNKNOWN", kTunerTypeUnknown },
    { NULL,      kTunerTypeUnknown },
};

const DTVParamHelperStruct DTVInversion::confTable[] =
{
   { "INVERSION_AUTO", kInversionAuto },
   { "INVERSION_OFF",  kInversionOff  },
   { "INVERSION_ON",   kInversionOn   },
   { NULL,             kInversionAuto },
};

const DTVParamHelperStruct DTVInversion::vdrTable[] =
{
   { "999", kInversionAuto },
   { "0",   kInversionOff  },
   { "1",   kInversionOn   },
   { NULL,  kInversionAuto },
};

const DTVParamHelperStruct DTVInversion::parseTable[] =
{
   { "a",  kInversionAuto },
   { "0",  kInversionOff  },
   { "1",  kInversionOn   },
   { NULL, kInversionAuto },
};

const char *DTVInversion::dbStr[DTVInversion::kDBStrCnt] =
{
    "0", ///< kInversionOff
    "1", ///< kInversionOn
    "a"  ///< kInversionAuto
};

const DTVParamHelperStruct DTVBandwidth::confTable[] =
{
   { "BANDWIDTH_AUTO",  kBandwidthAuto },
   { "BANDWIDTH_8_MHZ", kBandwidth8MHz },
   { "BANDWIDTH_7_MHZ", kBandwidth7MHz },
   { "BANDWIDTH_6_MHZ", kBandwidth6MHz },
   { NULL,              kBandwidthAuto },
};

const DTVParamHelperStruct DTVBandwidth::vdrTable[] =
{
   { "999", kBandwidthAuto },
   { "8",   kBandwidth8MHz },
   { "7",   kBandwidth7MHz },
   { "6",   kBandwidth6MHz },
   { NULL,  kBandwidthAuto },
};

const DTVParamHelperStruct DTVBandwidth::parseTable[] =
{
   { "a",    kBandwidthAuto },
   { "8",    kBandwidth8MHz },
   { "7",    kBandwidth7MHz },
   { "6",    kBandwidth6MHz },
   { NULL,   kBandwidthAuto },
};

const char *DTVBandwidth::dbStr[DTVBandwidth::kDBStrCnt] =
{
    "8",   ///< kBandwidth8MHz
    "7",   ///< kBandwidth7MHz
    "6",   ///< kBandwidth6MHz
    "a"    ///< kBandwidthAUTO
};

const DTVParamHelperStruct DTVCodeRate::confTable[] =
{
    { "FEC_AUTO", kFECAuto },
    { "FEC_1_2",  kFEC_1_2  },
    { "FEC_2_3",  kFEC_2_3  },
    { "FEC_3_4",  kFEC_3_4  },
    { "FEC_4_5",  kFEC_4_5  },
    { "FEC_5_6",  kFEC_5_6  },
    { "FEC_6_7",  kFEC_6_7  },
    { "FEC_7_8",  kFEC_7_8  },
    { "FEC_8_9",  kFEC_8_9  },
    { "FEC_NONE", kFECNone },
    { NULL,       kFECAuto },
};

const DTVParamHelperStruct DTVCodeRate::vdrTable[] =
{
    { "999", kFECAuto },
    { "12",  kFEC_1_2 },
    { "23",  kFEC_2_3 },
    { "34",  kFEC_3_4 },
    { "45",  kFEC_4_5 },
    { "56",  kFEC_5_6 },
    { "67",  kFEC_6_7 },
    { "78",  kFEC_7_8 },
    { "89",  kFEC_8_9 },
    { "0",   kFECNone },
    { NULL,  kFECAuto }
};

const DTVParamHelperStruct DTVCodeRate::parseTable[] =
{
    { "auto", kFECAuto },
    { "1/2",  kFEC_1_2 },
    { "2/3",  kFEC_2_3 },
    { "3/4",  kFEC_3_4 },
    { "4/5",  kFEC_4_5 },
    { "5/6",  kFEC_5_6 },
    { "6/7",  kFEC_6_7 },
    { "7/8",  kFEC_7_8 },
    { "8/9",  kFEC_8_9 },
    { "none", kFECNone },
    { NULL,   kFECAuto }
};

const char *DTVCodeRate::dbStr[DTVCodeRate::kDBStrCnt] =
{
     "none", ///< kFECNone
     "1/2",  ///< kFEC_1_2
     "2/3",  ///< kFEC_2_3
     "3/4",  ///< kFEC_3_4
     "4/5",  ///< kFEC_4_5
     "5/6",  ///< kFEC_5_6
     "6/7",  ///< kFEC_6_7
     "7/8",  ///< kFEC_7_8
     "8/9",  ///< kFEC_8_9
     "auto"  ///< kFECAuto
};

const DTVParamHelperStruct DTVModulation::confTable[] =
{
   { "QAM_AUTO", kModulationQAMAuto },
   { "QAM_16",   kModulationQAM16   },
   { "QAM_32",   kModulationQAM32   },
   { "QAM_64",   kModulationQAM64   },
   { "QAM_128",  kModulationQAM128  },
   { "QAM_256",  kModulationQAM256  },
   { "QPSK",     kModulationQPSK    },
   { "8VSB",     kModulation8VSB    },
   { "16VSB",    kModulation16VSB   },
   { "2VSB",     kModulation2VSB    },
   { "4VSB",     kModulation4VSB    },
   { "BPSK",     kModulationBPSK    },
   { "16APSK",   kModulation16APSK  },
   { "32APSK",   kModulation32APSK  },
   { "8PSK",     kModulation8PSK    },
   { "16PSK",    kModulation16PSK   },
   { "analog",   kModulation16PSK   },
   { NULL,       kModulationQAMAuto },
};

const DTVParamHelperStruct DTVModulation::vdrTable[] =
{
   { "999", kModulationQAMAuto },
   { "16",  kModulationQAM16   },
   { "32",  kModulationQAM32   },
   { "64",  kModulationQAM64   },
   { "128", kModulationQAM128  },
   { "256", kModulationQAM256  },
   { "0",   kModulationQPSK    },
   { NULL,  kModulationQAMAuto },
};

const DTVParamHelperStruct DTVModulation::parseTable[] =
{
   { "auto",     kModulationQAMAuto },
   { "qam_16",   kModulationQAM16   },
   { "qam_32",   kModulationQAM32   },
   { "qam_64",   kModulationQAM64   },
   { "qam_128",  kModulationQAM128  },
   { "qam_256",  kModulationQAM256  },
   { "qpsk",     kModulationQPSK    },
   { "8vsb",     kModulation8VSB    },
   { "16vsb",    kModulation16VSB   },
   { "2vsb",     kModulation2VSB    },
   { "4vsb",     kModulation4VSB    },
   { "bpsk",     kModulationBPSK    },
   { "16apsk",   kModulation16APSK  },
   { "32apsk",   kModulation32APSK  },
   { "8psk",     kModulation8PSK    },
   { "16psk",    kModulation16PSK   },
   // alternates
   { "a",        kModulationQAMAuto },
   { "qam_auto", kModulationQAMAuto },
   { "qam-16",   kModulationQAM16   },
   { "qam-32",   kModulationQAM32   },
   { "qam-64",   kModulationQAM64   },
   { "qam-128",  kModulationQAM128  },
   { "qam-256",  kModulationQAM256  },
   // qpsk, no alternative
   { "8-vsb",    kModulation8VSB    },
   { "16-vsb",   kModulation16VSB   },
   { "2-vsb",    kModulation2VSB    },
   { "4-vsb",    kModulation4VSB    },
   // bpsk, no alternative
   { "16-apsk",  kModulation16APSK  },
   { "32-apsk",  kModulation32APSK  },
   { "8-psk",    kModulation8PSK    },
   { "16-psk",   kModulation16PSK   },
   { NULL,       kModulationQAMAuto },
};

const char *DTVModulation::dbStr[DTVModulation::kDBStrCnt] =
{
    "qpsk",    ///< kModulationQPSK,
    "qam_16",  ///< kModulationQAM16
    "qam_32",  ///< kModulationQAM32
    "qam_64",  ///< kModulationQAM64
    "qam_128", ///< kModulationQAM128
    "qam_256", ///< kModulationQAM256
    "auto",    ///< kModulationQAMAuto
    "8vsb",    ///< kModulation8VSB
    "16vsb",   ///< kModulation16VSB
    "2vsb",    ///< kModulation2VSB
    "4vsb",    ///< kModulation4VSB
    "bpsk",    ///< kModulationBPSK
    "16apsk",  ///< kModulation16APSK
    "32apsk",  ///< kModulation32APSK
    "8psk",    ///< kModulation8PSK
    "16psk",   ///< kModulation16PSK
};

const DTVParamHelperStruct DTVTransmitMode::confTable[] =
{
   { "TRANSMISSION_MODE_AUTO", kTransmissionModeAuto },
   { "TRANSMISSION_MODE_2K",   kTransmissionMode2K   },
   { "TRANSMISSION_MODE_8K",   kTransmissionMode8K   },
   { NULL,                     kTransmissionModeAuto },
};

const DTVParamHelperStruct DTVTransmitMode::vdrTable[] =
{
   { "999", kTransmissionModeAuto },
   { "2",   kTransmissionMode2K   },
   { "8",   kTransmissionMode8K   },
   { NULL,  kTransmissionModeAuto },
};

const DTVParamHelperStruct DTVTransmitMode::parseTable[] =
{
   { "a",    kTransmissionModeAuto },
   { "2",    kTransmissionMode2K   },
   { "8",    kTransmissionMode8K   },
   { NULL,   kTransmissionModeAuto },
};

const char *DTVTransmitMode::dbStr[DTVTransmitMode::kDBStrCnt] =
{
    "2",   ///< kTransmissionMode2K
    "8",   ///< kTransmissionMode8K
    "a"    ///< kTransmissionModeAuto
};

const DTVParamHelperStruct DTVGuardInterval::confTable[] =
{
   { "GUARD_INTERVAL_AUTO", kGuardIntervalAuto  },
   { "GUARD_INTERVAL_1_32", kGuardInterval_1_32 },
   { "GUARD_INTERVAL_1_16", kGuardInterval_1_16 },
   { "GUARD_INTERVAL_1_8",  kGuardInterval_1_8  },
   { "GUARD_INTERVAL_1_4",  kGuardInterval_1_4  },
   { NULL,                  kGuardIntervalAuto  },
};

const DTVParamHelperStruct DTVGuardInterval::vdrTable[] =
{
   { "999", kGuardIntervalAuto  },
   { "32",  kGuardInterval_1_32 },
   { "16",  kGuardInterval_1_16 },
   { "8",   kGuardInterval_1_8  },
   { "4",   kGuardInterval_1_4  },
   { NULL,  kGuardIntervalAuto  },
};

const DTVParamHelperStruct DTVGuardInterval::parseTable[] =
{
   { "auto", kGuardIntervalAuto  },
   { "1/32", kGuardInterval_1_32 },
   { "1/16", kGuardInterval_1_16 },
   { "1/8",  kGuardInterval_1_8  },
   { "1/4",  kGuardInterval_1_4  },
   { NULL,   kGuardIntervalAuto  },
};

const char *DTVGuardInterval::dbStr[DTVGuardInterval::kDBStrCnt] =
{
    "1/32", ///< kGuardInterval_1_32
    "1/16", ///< kGuardInterval_1_16
    "1/8",  ///< kGuardInterval_1_8
    "1/4",  ///< kGuardInterval_1_4
    "auto"  ///< kGuardIntervalAuto
};

const DTVParamHelperStruct DTVHierarchy::confTable[] =
{
   { "HIERARCHY_NONE", kHierarchyNone },
   { "HIERARCHY_1",    kHierarchy1    },
   { "HIERARCHY_2",    kHierarchy2    },
   { "HIERARCHY_4",    kHierarchy4    },
   { "HIERARCHY_AUTO", kHierarchyAuto },
   { NULL,             kHierarchyAuto },
};

const DTVParamHelperStruct DTVHierarchy::vdrTable[] =
{
   { "0",   kHierarchyNone },
   { "1",   kHierarchy1    },
   { "2",   kHierarchy2    },
   { "4",   kHierarchy4    },
   { "999", kHierarchyAuto },
   { NULL,  kHierarchyAuto },
};

const DTVParamHelperStruct DTVHierarchy::parseTable[] =
{
   { "n",  kHierarchyNone },
   { "1",  kHierarchy1    },
   { "2",  kHierarchy2    },
   { "4",  kHierarchy4    },
   { "a",  kHierarchyAuto },
   { NULL, kHierarchyAuto },
};

const char *DTVHierarchy::dbStr[DTVHierarchy::kDBStrCnt] =
{
    "n", ///< kHierarchyNone
    "1", ///< kHierarchy1
    "2", ///< kHierarchy2
    "4", ///< kHierarchy4
    "a"  ///< kHierarchyAuto
};

const DTVParamHelperStruct DTVPolarity::parseTable[] =
{
    { "v",  kPolarityVertical   },
    { "h",  kPolarityHorizontal },
    { "r",  kPolarityRight      },
    { "l",  kPolarityLeft       },
    { NULL, kPolarityVertical   },
};

const char *DTVPolarity::dbStr[DTVPolarity::kDBStrCnt] =
{
   "v", ///< kPolarityVertical
   "h", ///< kPolarityHorizontal
   "r", ///< kPolarityRight
   "l"  ///< kPolarityLeft
};
