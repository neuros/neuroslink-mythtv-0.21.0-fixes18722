#include "frequencies.h"
#include "frequencytables.h"
#include "channelutil.h"
#include "compat.h"

freq_table_map_t frequencies;

static void init_freq_tables(freq_table_map_t&);

TransportScanItem::TransportScanItem()
    : mplexid((uint)-1),  FriendlyName(""),
      friendlyNum(0),     SourceID(0),          UseTimer(false),
      scanning(false),    timeoutTune(1000)
{ 
    bzero(freq_offsets, sizeof(int)*3);

    tuning.Clear();
}

TransportScanItem::TransportScanItem(uint           sourceid,
                                     const QString &_si_std,
                                     const QString &_name,
                                     uint           _mplexid,
                                     uint           _timeoutTune)
    : mplexid(_mplexid),  FriendlyName(_name),
      friendlyNum(0),     SourceID(sourceid),   UseTimer(false),
      scanning(false),    timeoutTune(_timeoutTune)
{
    bzero(freq_offsets, sizeof(int)*3);

    tuning.Clear();
    tuning.sistandard = _si_std;
}

TransportScanItem::TransportScanItem(uint           _sourceid,
                                     const QString &_name,
                                     DTVMultiplex  &_tuning,
                                     uint           _timeoutTune)
    : mplexid(0),
      FriendlyName(_name), friendlyNum(0),
      SourceID(_sourceid), UseTimer(false),
      scanning(false),     timeoutTune(_timeoutTune)
{
    bzero(freq_offsets, sizeof(int) * 3);
    tuning = _tuning;
}

TransportScanItem::TransportScanItem(uint                _sourceid,
                                     const QString      &_name,
                                     DTVTunerType        _tuner_type,
                                     const DTVTransport &_tuning,
                                     uint                _timeoutTune)
    : mplexid(0),
      FriendlyName(_name), friendlyNum(0),
      SourceID(_sourceid), UseTimer(false),
      scanning(false),     timeoutTune(_timeoutTune)
{
    bzero(freq_offsets, sizeof(int) * 3); 
    expectedChannels = _tuning.channels;

    tuning.Clear();

    tuning.ParseTuningParams(
        _tuner_type,
        QString::number(_tuning.frequency),  _tuning.inversion.toString(),
        QString::number(_tuning.symbolrate), _tuning.fec.toString(),
        _tuning.polarity.toString(),         _tuning.hp_code_rate.toString(),
        _tuning.lp_code_rate.toString(),     _tuning.modulation.toString(),
        _tuning.trans_mode.toString(),       _tuning.guard_interval.toString(),
        _tuning.hierarchy.toString(),        _tuning.modulation.toString(),
        _tuning.bandwidth.toString());
}

TransportScanItem::TransportScanItem(uint sourceid,
                                     const QString &std,
                                     const QString &fn,
                                     uint fnum,
                                     uint freq,
                                     const FrequencyTable &ft,
                                     uint tuneTO)
    : mplexid(0),         FriendlyName(fn),
      friendlyNum(fnum),  SourceID(sourceid),   UseTimer(false),
      scanning(false),    timeoutTune(tuneTO)
{
    bzero(freq_offsets, sizeof(int)*3);

    tuning.Clear();

    // setup tuning params
    tuning.frequency  = freq;
    tuning.sistandard = std;
    freq_offsets[1]   = ft.offset1;
    freq_offsets[2]   = ft.offset2;

    if (std == "dvb")
    {
        tuning.inversion      = ft.inversion;
        tuning.bandwidth      = ft.bandwidth;
        tuning.hp_code_rate   = ft.coderate_hp;
        tuning.lp_code_rate   = ft.coderate_lp;
        tuning.modulation     = ft.modulation;
        tuning.trans_mode     = ft.trans_mode;
        tuning.guard_interval = ft.guard_interval;
        tuning.hierarchy      = ft.hierarchy;
    }
    else
    {
        tuning.modulation     = ft.modulation;
    }

    mplexid = GetMultiplexIdFromDB();
}

/** \fn TransportScanItem::GetMultiplexIdFromDB(void) const
 *  \brief Fetches mplexid if it exists, based on the frequency and sourceid
 */
uint TransportScanItem::GetMultiplexIdFromDB(void) const
{
    int mplexid = 0;

    for (uint i = 0; (i < offset_cnt()) && (mplexid <= 0); i++)
        mplexid = ChannelUtil::GetMplexID(SourceID, freq_offset(i));

    return mplexid < 0 ? 0 : mplexid;
}

uint64_t TransportScanItem::freq_offset(uint i) const
{
    int64_t freq = (int64_t) tuning.frequency;

    return (uint64_t) (freq + freq_offsets[i]);
}

QString TransportScanItem::toString() const
{
    QString str = QString("Transport Scan Item '%1' #%2\n")
        .arg(FriendlyName).arg(friendlyNum);
    str += QString("\tmplexid(%1) standard(%2) sourceid(%3)\n")
        .arg(mplexid).arg(tuning.sistandard).arg(SourceID);
    str += QString("\tUseTimer(%1) scanning(%2)\n")
        .arg(UseTimer).arg(scanning);
    str += QString("\ttimeoutTune(%3 msec)\n").arg(timeoutTune);
    if (tuning.sistandard == "atsc")
    {
        str += QString("\tfrequency(%1) modulation(%2)\n")
            .arg(tuning.frequency)
            .arg(tuning.modulation);
    }
    else
    {
        str += QString("\tfrequency(%1) constellation(%2)\n")
            .arg(tuning.frequency)
            .arg(tuning.modulation);
        str += QString("\t  inv(%1) bandwidth(%2) hp(%3) lp(%4)\n")
            .arg(tuning.inversion)
            .arg(tuning.bandwidth)
            .arg(tuning.hp_code_rate)
            .arg(tuning.lp_code_rate);
        str += QString("\t  trans_mode(%1) guard_int(%2) hierarchy(%3)\n")
            .arg(tuning.trans_mode)
            .arg(tuning.guard_interval)
            .arg(tuning.hierarchy);
    }
    str += QString("\t offset[0..2]: %1 %2 %3")
        .arg(freq_offsets[0]).arg(freq_offsets[1]).arg(freq_offsets[2]);
    return str;
}

bool init_freq_tables()
{
    static bool statics_initialized = false;
    static QMutex statics_lock;
    statics_lock.lock();
    if (!statics_initialized)
    {
        init_freq_tables(frequencies);
        statics_initialized = true;
    }
    statics_lock.unlock();

    return true;
}
bool just_here_to_force_init = init_freq_tables();

freq_table_list_t get_matching_freq_tables(
    QString format, QString modulation, QString country)
{
    const freq_table_map_t &fmap = frequencies;

    freq_table_list_t list;

    QString lookup = QString("%1_%2_%3%4")
        .arg(format).arg(modulation).arg(country);

    freq_table_map_t::const_iterator it = fmap.begin();
    for (uint i = 0; it != fmap.end(); i++)
    {
        it = fmap.find(lookup.arg(i));
        if (it != fmap.end())
            list.push_back(*it);
    }

    return list;
}

long long get_center_frequency(
    QString format, QString modulation, QString country, int freqid)
{
    freq_table_list_t list =
        get_matching_freq_tables(format, modulation, country);

    for (uint i = 0; i < list.size(); ++i)
    {
        int min_freqid = list[i]->name_offset;
        int max_freqid = min_freqid +
            ((list[i]->frequencyEnd - list[i]->frequencyStart) /
             list[i]->frequencyStep);

        if ((min_freqid <= freqid) && (freqid <= max_freqid))
            return list[i]->frequencyStart +
                list[i]->frequencyStep * (freqid - min_freqid);
    }
    return -1;
}

int get_closest_freqid(
    QString format, QString modulation, QString country, long long centerfreq)
{
    modulation = (modulation == "8vsb") ? "vsb8" : modulation;

    freq_table_list_t list =
        get_matching_freq_tables(format, modulation, country);
    
    for (uint i = 0; i < list.size(); ++i)
    {
        int min_freqid = list[i]->name_offset;
        int max_freqid = min_freqid +
            ((list[i]->frequencyEnd - list[i]->frequencyStart) /
             list[i]->frequencyStep);
        int freqid =
            ((centerfreq - list[i]->frequencyStart) /
             list[i]->frequencyStep) + min_freqid;

        if ((min_freqid <= freqid) && (freqid <= max_freqid))
            return freqid;
    }
    //VERBOSE(VB_IMPORTANT, "get_closest_freqid("<<format<<", "
    //        <<modulation<<", "<<country<<", "<<centerfreq
    //        <<") Failed sz("<<list.size()<<")");
    return -1;
}


static void init_freq_tables(freq_table_map_t &fmap)
{
    // United Kingdom
    fmap["dvbt_ofdm_uk0"] = new FrequencyTable(
        474000000, 850000000, 8000000, "" , 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth8MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAMAuto,
        DTVTransmitMode::kTransmissionMode2K,
        DTVGuardInterval::kGuardInterval_1_32, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAMAuto, 166670, -166670);

    // Finland
    fmap["dvbt_ofdm_fi0"] = new FrequencyTable(
        474000000, 850000000, 8000000, "", 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth8MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAM64,
        DTVTransmitMode::kTransmissionModeAuto,
        DTVGuardInterval::kGuardIntervalAuto, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAMAuto, 0, 0);

    // Sweden
    fmap["dvbt_ofdm_se0"] = new FrequencyTable(
        474000000, 850000000, 8000000, "", 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth8MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAM64,
        DTVTransmitMode::kTransmissionModeAuto,
        DTVGuardInterval::kGuardIntervalAuto, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAMAuto, 0, 0);

    // Australia
    fmap["dvbt_ofdm_au0"] = new FrequencyTable(
        177500000, 226500000, 7000000, "", 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth7MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAM64,
        DTVTransmitMode::kTransmissionMode8K, DTVGuardInterval::kGuardIntervalAuto, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAMAuto, 125000, 0); // VHF 6-12
    fmap["dvbt_ofdm_au1"] = new FrequencyTable(
        529500000, 816500000, 7000000, "", 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth7MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAM64,
        DTVTransmitMode::kTransmissionMode8K,
        DTVGuardInterval::kGuardIntervalAuto, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAMAuto, 125000, 0); // UHF 28-69

    // Germany (Deuschland)
    fmap["dvbt_ofdm_de0"] = new FrequencyTable(
        177500000, 226500000, 7000000, "", 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth7MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAMAuto,
        DTVTransmitMode::kTransmissionMode8K,
        DTVGuardInterval::kGuardIntervalAuto, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAMAuto, 125000, 0); // VHF 6-12
    fmap["dvbt_ofdm_de1"] = new FrequencyTable(
        474000000, 826000000, 8000000, "", 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth8MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAMAuto,
        DTVTransmitMode::kTransmissionModeAuto,
        DTVGuardInterval::kGuardIntervalAuto, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAMAuto, 125000, 0); // UHF 21-65

    // Spain
    fmap["dvbt_ofdm_es0"] = new FrequencyTable(
        474000000, 858000000, 8000000, "", 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth8MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAMAuto,
        DTVTransmitMode::kTransmissionModeAuto,
        DTVGuardInterval::kGuardIntervalAuto, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAMAuto, 125000, 0); // UHF 21-69

    // New Zealand
    fmap["dvbt_ofdm_nz0"] = new FrequencyTable(
        474000000, 858000000, 8000000, "", 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth8MHz, DTVCodeRate::kFEC_3_4,
        DTVCodeRate::kFEC_3_4, DTVModulation::kModulationQAM64,
        DTVTransmitMode::kTransmissionMode8K,
        DTVGuardInterval::kGuardInterval_1_16, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAM64, 0 , 0); // UHF 21-69

    // france
    fmap["dvbt_ofdm_fr0"] = new FrequencyTable(
        474000000, 850000000, 8000000, "" , 0, DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth8MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAMAuto,
        DTVTransmitMode::kTransmissionMode8K,
        DTVGuardInterval::kGuardIntervalAuto, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulationQAMAuto, 167000, -166000);
    
//#define DEBUG_DVB_OFFSETS
#ifdef DEBUG_DVB_OFFSETS
    // UHF 14-69
    fmap["atsc_vsb8_us0"] = new FrequencyTable(
        533000000, 803000000, 6000000, "xATSC Channel %1", 24,
        DTVInversion::kInversionOff,
        DTVBandwidth::kBandwidth7MHz, DTVCodeRate::kFECAuto,
        DTVCodeRate::kFECAuto, DTVModulation::kModulationQAMAuto,
        DTVTransmitMode::kTransmissionMode8K,
        DTVGuardInterval::kGuardIntervalAuto, DTVHierarchy::kHierarchyNone,
        DTVModulation::kModulation8VSB, -100000, 100000);
#else // if !DEBUG_DVB_OFFSETS
    // USA Terrestrial (center frequency, subtract 1.75 MHz for visual carrier)
    // VHF 2-4
    fmap["atsc_vsb8_us0"] = new FrequencyTable(
        "ATSC Channel %1",  2,  57000000,  69000000, 6000000,
        DTVModulation::kModulation8VSB);
    // VHF 5-6
    fmap["atsc_vsb8_us1"] = new FrequencyTable(
        "ATSC Channel %1",  5,  79000000,  85000000, 6000000,
        DTVModulation::kModulation8VSB);
    // VHF 7-13
    fmap["atsc_vsb8_us2"] = new FrequencyTable(
        "ATSC Channel %1",  7, 177000000, 213000000, 6000000,
        DTVModulation::kModulation8VSB);
    // UHF 14-69
    fmap["atsc_vsb8_us3"] = new FrequencyTable(
        "ATSC Channel %1", 14, 473000000, 803000000, 6000000,
        DTVModulation::kModulation8VSB);
    // UHF 70-83
    fmap["atsc_vsb8_us4"] = new FrequencyTable(
        "ATSC Channel %1", 70, 809000000, 887000000, 6000000,
        DTVModulation::kModulation8VSB);
#endif // !DEBUG_DVB_OFFSETS

    QString modStr[] = { "vsb8",  "qam256",   "qam128",   "qam64",   };
    uint    mod[]    = { DTVModulation::kModulation8VSB,
                         DTVModulation::kModulationQAM256,
                         DTVModulation::kModulationQAM128,
                         DTVModulation::kModulationQAM64, };
    QString desc[]   = { "ATSC ", "QAM-256 ", "QAM-128 ", "QAM-64 ", };

#define FREQ(A,B, C,D, E,F,G, H, I) \
    fmap[QString("atsc_%1_us%2").arg(A).arg(B)] = \
        new FrequencyTable(C+D, E, F, G, H, I);

// The maximum channel defined in the US frequency tables (standard, HRC, IRC)
#define US_MAX_CHAN 159
// Equation for computing EIA-542 frequency of channels > 99
// A = bandwidth, B = offset, C = channel designation (number)
#define EIA_542_FREQUENCY(A,B,C) ( ( A * ( 8 + C ) ) + B )

    for (uint i = 0; i < 4; i++)
    {
        // USA Cable, ch 2 to US_MAX_CHAN and T.7 to T.14
        FREQ(modStr[i], "cable0", desc[i], "Channel %1",
             2,    57000000,   69000000, 6000000, mod[i]); // 2-4
        FREQ(modStr[i], "cable1", desc[i], "Channel %1",
             5,    79000000,   85000000, 6000000, mod[i]); // 5-6
        FREQ(modStr[i], "cable2", desc[i], "Channel %1",
             7,   177000000,  213000000, 6000000, mod[i]); // 7-13
        FREQ(modStr[i], "cable3", desc[i], "Channel %1",
             14,  123000000,  171000000, 6000000, mod[i]); // 14-22
        FREQ(modStr[i], "cable4", desc[i], "Channel %1",
             23,  219000000,  645000000, 6000000, mod[i]); // 23-94
        FREQ(modStr[i], "cable5", desc[i], "Channel %1",
             95,   93000000,  117000000, 6000000, mod[i]); // 95-99
        // The center frequency of any EIA-542 std cable channel over 99 is
        // Frequency_MHz = ( 6 * ( 8 + channel_designation ) ) + 3
        FREQ(modStr[i], "cable6", desc[i], "Channel %1",
             100, 651000000,
             EIA_542_FREQUENCY(6000000, 3000000, US_MAX_CHAN),
             6000000, mod[i]);                             // 100-US_MAX_CHAN
        FREQ(modStr[i], "cable7", desc[i], "Channel T-%1",
             7,    8750000,   50750000, 6000000, mod[i]); // T7-14

        // USA Cable, QAM 256 ch 78 to US_MAX_CHAN
        FREQ(modStr[i], "cablehigh0", desc[i], "Channel %1",
             78,  549000000,  645000000, 6000000, mod[i]); // 78-94
        FREQ(modStr[i], "cablehigh1", desc[i], "Channel %1",
             100, 651000000,
             EIA_542_FREQUENCY(6000000, 3000000, US_MAX_CHAN),
             6000000, mod[i]);                             // 100-US_MAX_CHAN

        // USA Cable HRC, ch 1 to US_MAX_CHAN
        FREQ(modStr[i], "hrc0", desc[i], "HRC %1",
             1,    73753600,  73753601, 6000300, mod[i]); // 1
        FREQ(modStr[i], "hrc1", desc[i], "HRC %1",
             2,    55752700,  67753300, 6000300, mod[i]); // 2-4
        FREQ(modStr[i], "hrc2", desc[i], "HRC %1",
             5,    79753900,  85754200, 6000300, mod[i]); // 5-6
        FREQ(modStr[i], "hrc3", desc[i], "HRC %1",
             7,   175758700, 211760500, 6000300, mod[i]); // 7-13
        FREQ(modStr[i], "hrc4", desc[i], "HRC %1",
             14,  121756000, 169758400, 6000300, mod[i]); // 14-22
        FREQ(modStr[i], "hrc5", desc[i], "HRC %1",
             23,  217760800, 643782100, 6000300, mod[i]); // 23-94
        FREQ(modStr[i], "hrc6", desc[i], "HRC %1",
             95,   91754500, 115755700, 6000300, mod[i]); // 95-99
        // The center frequency of any EIA-542 HRC cable channel over 99 is
        // Frequency_MHz = ( 6.0003 * ( 8 + channel_designation ) ) + 1.75
        FREQ(modStr[i], "hrc7", desc[i], "HRC %1",
             100, 649782400,
             EIA_542_FREQUENCY(6000300, 1750000, US_MAX_CHAN),
             6000300, mod[i]); // 100-US_MAX_CHAN

        // USA Cable HRC, ch 76-94 and 100-US_MAX_CHAN
        // Channels 95-99 are low frequency despite high channel numbers
        FREQ(modStr[i], "hrchigh0", desc[i], "HRC %1",
             76,  535776700, 643782100, 6000300, mod[i]); // 76-94
        FREQ(modStr[i], "hrchigh1", desc[i], "HRC %1",
             100, 649782400,
             EIA_542_FREQUENCY(6000300, 1750000, US_MAX_CHAN),
             6000300, mod[i]); // 100-US_MAX_CHAN

        // USA Cable IRC, ch 1 to US_MAX_CHAN
        FREQ(modStr[i], "irc0", desc[i], "IRC %1",
             1,    75012500,  75012501, 6000000, mod[i]); // 1
        FREQ(modStr[i], "irc1", desc[i], "IRC %1",
             2,    57012500,  69012500, 6000000, mod[i]); // 2-4
        FREQ(modStr[i], "irc2", desc[i], "IRC %1",
             5,    81012500,  87012500, 6000000, mod[i]); // 5-6
        FREQ(modStr[i], "irc3", desc[i], "IRC %1",
             7,   177012500, 213012500, 6000000, mod[i]); // 7-13
        FREQ(modStr[i], "irc4", desc[i], "IRC %1",
             14,  123012500, 171012500, 6000000, mod[i]); // 14-22
        FREQ(modStr[i], "irc5", desc[i], "IRC %1",
             23,  219012500, 327012500, 6000000, mod[i]); // 23-41
        FREQ(modStr[i], "irc6", desc[i], "IRC %1",
             42,  333025000, 333025001, 6000000, mod[i]); // 42
        FREQ(modStr[i], "irc7", desc[i], "IRC %1",
             43,  339012500, 645012500, 6000000, mod[i]); // 43-94
        FREQ(modStr[i], "irc8", desc[i], "IRC %1",
             95,   93012500, 105012500, 6000000, mod[i]); // 95-97
        FREQ(modStr[i], "irc9", desc[i], "IRC %1",
             98,  111025000, 117025000, 6000000, mod[i]); // 98-99
        // The center frequency of any EIA-542 IRC cable channel over 99 is
        // Frequency_MHz = ( 6 * ( 8 + channel_designation ) ) + 3.0125
        FREQ(modStr[i], "irc10", desc[i], "IRC %1",
             100, 651012500,
             EIA_542_FREQUENCY(6000000, 3012500, US_MAX_CHAN),
             6000000, mod[i]); // 100-US_MAX_CHAN

        // USA Cable IRC, ch 76-94 and 100-125
        // Channels 95-99 are low frequency despite high channel numbers
        FREQ(modStr[i], "irchigh0", desc[i], "IRC %1",
             76,  537012500, 645012500, 6000000, mod[i]); // 76-94
        FREQ(modStr[i], "irchigh1", desc[i], "IRC %1",
             100, 651012500,
             EIA_542_FREQUENCY(6000000, 3012500, US_MAX_CHAN),
             6000000, mod[i]); // 100-US_MAX_CHAN
    }

    // create old school frequency tables...
    for (struct CHANLISTS *ptr = chanlists; ptr->name ; ptr++)
    {
        QString tbl_name = ptr->name;
        for (uint i = 0; i < (uint)ptr->count; i++)
        {
            uint64_t freq = (ptr->list[i].freq * 1000LL) + 1750000;
            fmap[QString("analog_analog_%1%2").arg(tbl_name).arg(i)] =
                new FrequencyTable(
                    QString("%1 %2").arg(tbl_name).arg(ptr->list[i].name), i+2,
                    freq, freq + 3000000,
                    6000000, DTVModulation::kModulationAnalog);
        }
    }

}