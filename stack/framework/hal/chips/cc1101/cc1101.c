/* \file
 *
 * Driver for TexasInstruments cc1101 radio. This driver is also used for TI cc430 which a SoC containing a cc1101.
 * Nearly all of the logic is shared, except for the way of communicating with the chip, which is done using SPI and GPIO for
 * an external cc1101 versus using registers for cc430. The specifics parts are contained in cc1101_interface{spi/cc430}.c
 *
 * @author glenn.ergeerts@uantwerpen.be
 *
 */

#include "assert.h"

#include "log.h"
#include "hwradio.h"

#include "cc1101.h"
#include "cc1101_interface.h"
#include "cc1101_constants.h"
#include "cc1101_registers.h"

// turn on/off the debug prints
#ifdef FRAMEWORK_LOG_ENABLED // TODO more granular (LOG_PHY_ENABLED)
#define DPRINT(...) log_print_string(__VA_ARGS__)
#else
#define DPRINT(...)
#endif

static alloc_packet_callback_t alloc_packet_callback;
static release_packet_callback_t release_packet_callback;
static rx_packet_callback_t rx_packet_callback;
static tx_packet_callback_t tx_packet_callback;
static rssi_valid_callback_t rssi_valid_callback;

static hw_radio_state_t current_state;
static hw_radio_packet_t* current_packet;

static RF_SETTINGS rf_settings = {
   RADIO_GDO2_VALUE,   			// IOCFG2    GDO2 output pin configuration.
   RADIO_GDO1_VALUE,    			// IOCFG1    GDO1 output pin configuration.
   RADIO_GDO0_VALUE,   			// IOCFG0    GDO0 output pin configuration.
   RADIO_FIFOTHR_FIFO_THR_61_4,   	// FIFOTHR   RXFIFO and TXFIFO thresholds.
   RADIO_SYNC1,     				// SYNC1	 Sync word, high byte
   RADIO_SYNC0,     				// SYNC0	 Sync word, low byte
   RADIO_PKTLEN,    				// PKTLEN    Packet length.
   RADIO_PKTCTRL1_PQT(3) | RADIO_PKTCTRL1_APPEND_STATUS,   // PKTCTRL1  Packet automation control.
   RADIO_PKTCTRL0_WHITE_DATA | RADIO_PKTCTRL0_LENGTH_VAR,      // PKTCTRL0  Packet automation control.
   RADIO_ADDR,   					// ADDR      Device address.
   RADIO_CHAN,   					// CHANNR    Channel number.
   RADIO_FREQ_IF,   				// FSCTRL1   Frequency synthesizer control.
   RADIO_FREQOFF,   				// FSCTRL0   Frequency synthesizer control.
   RADIO_FREQ2,   					// FREQ2     Frequency control word, high byte.
   RADIO_FREQ1,   					// FREQ1     Frequency control word, middle byte.
   RADIO_FREQ0,   					// FREQ0     Frequency control word, low byte.
   RADIO_MDMCFG4_CHANBW_E(1) | RADIO_MDMCFG4_CHANBW_M(0) | RADIO_MDMCFG4_DRATE_E(11),   // MDMCFG4   Modem configuration.
   RADIO_MDMCFG3_DRATE_M(24),   	// MDMCFG3   Modem configuration.
   RADIO_MDMCFG2_DEM_DCFILT_ON | RADIO_MDMCFG2_MOD_FORMAT_GFSK | RADIO_MDMCFG2_SYNC_MODE_16in16CS,   // MDMCFG2   Modem configuration.
   RADIO_MDMCFG1_NUM_PREAMBLE_4B | RADIO_MDMCFG1_CHANSPC_E(2),   // MDMCFG1   Modem configuration.
   RADIO_MDMCFG0_CHANSPC_M(16),   	// MDMCFG0   Modem configuration.
   RADIO_DEVIATN_E(5) | RADIO_DEVIATN_M(0),   // DEVIATN   Modem deviation setting (when FSK modulation is enabled).
   RADIO_MCSM2_RX_TIME(7),			// MCSM2		 Main Radio Control State Machine configuration.
   RADIO_MCSM1_CCA_RSSILOWRX | RADIO_MCSM1_RXOFF_MODE_IDLE | RADIO_MCSM1_TXOFF_MODE_IDLE,	// MCSM1 Main Radio Control State Machine configuration.
   //RADIO_MCSM0_FS_AUTOCAL_FROMIDLE,// MCSM0     Main Radio Control State Machine configuration.
   RADIO_MCSM0_FS_AUTOCAL_4THIDLE,// MCSM0     Main Radio Control State Machine configuration.
   RADIO_FOCCFG_FOC_PRE_K_3K | RADIO_FOCCFG_FOC_POST_K_HALFK | RADIO_FOCCFG_FOC_LIMIT_4THBW,   // FOCCFG    Frequency Offset Compensation Configuration.
   RADIO_BSCFG_BS_PRE_KI_2KI | RADIO_BSCFG_BS_PRE_KP_3KP | RADIO_BSCFG_BS_POST_KI_1KP | RADIO_BSCFG_BS_POST_KP_1KP | RADIO_BSCFG_BS_LIMIT_0,   // BSCFG     Bit synchronization Configuration.
   RADIO_AGCCTRL2_MAX_DVGA_GAIN_ALL | RADIO_AGCCTRL2_MAX_LNA_GAIN_SUB0 | RADIO_AGCCTRL2_MAX_MAGN_TARGET_33,   // AGCCTRL2  AGC control.
   RADIO_AGCCTRL1_AGC_LNA_PRIORITY | RADIO_AGCCTRL1_CS_REL_THR_DISABLED | RADIO_AGCCTRL1_CS_ABS_THR_FLAT,   // AGCCTRL1  AGC control.
   RADIO_AGCCTRL0_HYST_LEVEL_MED | RADIO_AGCCTRL0_WAIT_ITME_16 | RADIO_AGCCTRL0_AGC_FREEZE_NORMAL | RADIO_AGCCTRL0_FILTER_LENGTH_16,   // AGCCTRL0  AGC control.
   RADIO_WOREVT1_EVENT0_HI(128), 	// WOREVT1
   RADIO_WOREVT0_EVENT0_LO(0),		// WOREVT0
   RADIO_WORCTRL_ALCK_PD,			// WORCTRL
   RADIO_FREND1_LNA_CURRENT(1) | RADIO_FREND1_LNA2MIX_CURRENT(1) | RADIO_FREND1_LODIV_BUF_CURRENT_RX(1) | RADIO_FREND1_MIX_CURRENT(2),   // FREND1    Front end RX configuration.
   RADIO_FREND0_LODIV_BUF_CURRENT_TX(1) | RADIO_FREND0_PA_POWER(0),   // FREND0    Front end TX configuration.
   RADIO_FSCAL3_HI(3) | RADIO_FSCAL3_CHP_CURR_CAL_EN(2) | RADIO_FSCAL3_LO(10),   // FSCAL3    Frequency synthesizer calibration.
   RADIO_FSCAL2_FSCAL2(10),   		// FSCAL2    Frequency synthesizer calibration.
   RADIO_FSCAL1(0),   				// FSCAL1    Frequency synthesizer calibration.
   RADIO_FSCAL0(31)   				// FSCAL0    Frequency synthesizer calibration.
};

static void switch_to_idle_mode()
{
    //Flush FIFOs and go to sleep
    cc1101_interface_strobe(RF_SFRX); // TODO cc1101 datasheet : Only issue SFRX in IDLE or RXFIFO_OVERFLOW states
    cc1101_interface_strobe(RF_SFTX); // TODO cc1101 datasheet : Only issue SFTX in IDLE or TXFIFO_UNDERFLOW states.
    cc1101_interface_strobe(RF_SIDLE);
    cc1101_interface_strobe(RF_SPWD);
    current_state = HW_RADIO_STATE_IDLE;
}

static void end_of_packet_isr()
{
    cc1101_interface_set_interrupts_enabled(false);
    DPRINT("end of packet ISR");
    if (current_state == HW_RADIO_STATE_RX)
    {
        // TODO
    }
    if (current_state == HW_RADIO_STATE_TX)
    {
        assert(tx_packet_callback != NULL);
        // TODO fill metadata
        tx_packet_callback(current_packet);
    }
    else
    {
        assert(false);
    }

    switch_to_idle_mode();
}

static void configure_channel()
{
    // TODO compare with previous cfg, don't set when not needed
    hw_tx_cfg_t cfg = (hw_tx_cfg_t)current_packet->tx_meta.tx_cfg;

    assert(cfg.channel_id.ch_freq_band == PHY_BAND_433); // TODO implement other bands
    assert(cfg.channel_id.ch_class == PHY_CLASS_NORMAL_RATE); // TODO implement other rates
    assert(cfg.channel_id.ch_coding == PHY_CODING_PN9); // TODO implement other codings
    // TODO assert valid center freq index

    // TODO preamble size depends on channel class

    // set freq band
    DPRINT("Set frequency band index: %d", cfg.channel_id.ch_freq_band);
    // TODO validate
    /*
    switch(frequency_band)
        {
        case 0:
            WriteSingleReg(RADIO_FREQ2, (uint8_t)(RADIO_FREQ_433>>16 & 0xFF));
            WriteSingleReg(RADIO_FREQ1, (uint8_t)(RADIO_FREQ_433>>8 & 0xFF));
            WriteSingleReg(RADIO_FREQ0, (uint8_t)(RADIO_FREQ_433 & 0xFF));
            break;
        case 1:
            WriteSingleReg(RADIO_FREQ2, (uint8_t)(RADIO_FREQ_868>>16 & 0xFF));
            WriteSingleReg(RADIO_FREQ1, (uint8_t)(RADIO_FREQ_868>>8 & 0xFF));
            WriteSingleReg(RADIO_FREQ0, (uint8_t)(RADIO_FREQ_868 & 0xFF));
            break;
        case 2:
            WriteSingleReg(RADIO_FREQ2, (uint8_t)(RADIO_FREQ_915>>16 & 0xFF));
            WriteSingleReg(RADIO_FREQ1, (uint8_t)(RADIO_FREQ_915>>8 & 0xFF));
            WriteSingleReg(RADIO_FREQ0, (uint8_t)(RADIO_FREQ_915 & 0xFF));
            break;
        }
    */

    // set channel center frequency
    DPRINT("Set channel freq index: %d", cfg.channel_id.center_freq_index);
    cc1101_interface_write_single_reg(CHANNR, cfg.channel_id.center_freq_index); // TODO validate

    // set modulation, symbol rate and deviation
    switch(cfg.channel_id.ch_class)
    {
        case PHY_CLASS_NORMAL_RATE:
            // TODO validate
            cc1101_interface_write_single_reg(MDMCFG3, RADIO_MDMCFG3_DRATE_M(24));
            cc1101_interface_write_single_reg(MDMCFG4, (RADIO_MDMCFG4_CHANBW_E(1) | RADIO_MDMCFG4_CHANBW_M(0) | RADIO_MDMCFG4_DRATE_E(11)));
            cc1101_interface_write_single_reg(DEVIATN, (RADIO_DEVIATN_E(5) | RADIO_DEVIATN_M(0)));
            break;
            // TODO: other classes
    }

    // TODO set EIRP

    cc1101_interface_strobe(RF_SCAL); // TODO is this the right case?
}

error_t hw_radio_init(alloc_packet_callback_t alloc_packet_cb,
                      release_packet_callback_t release_packet_cb,
                      rx_packet_callback_t rx_cb,
                      tx_packet_callback_t tx_cb,
                      rssi_valid_callback_t rssi_valid_cb)
{
    alloc_packet_callback = alloc_packet_cb;
    release_packet_callback = release_packet_cb;
    rx_packet_callback = rx_cb;
    tx_packet_callback = tx_cb;
    rssi_valid_callback = rssi_valid_cb;

    current_state = HW_RADIO_STATE_IDLE;

    cc1101_interface_init(&end_of_packet_isr);
    cc1101_interface_reset_radio_core();
    cc1101_interface_write_rfsettings(&rf_settings);

    DPRINT("RF settings:");
    uint8_t* p = (uint8_t*) &rf_settings;
    uint8_t i;
    for(i = 0; i < sizeof(RF_SETTINGS); i++)
    {
        DPRINT("\t0x%02X", p[i]);
    }

    // TODO
//    last_tx_cfg.eirp = 0;
//    last_tx_cfg.spectrum_id[0] = 0;
//    last_tx_cfg.spectrum_id[1] = 0;
//    last_tx_cfg.sync_word_class = 0;
}

error_t hw_radio_set_rx(hw_rx_cfg_t const* rx_cfg)
{
    // TODO
}

error_t hw_radio_send_packet(hw_radio_packet_t* packet)
{
    // TODO error handling
    // TODO what if TX is already in progress?
    // TODO set channel

    current_state = HW_RADIO_STATE_TX;
    current_packet = packet;
    cc1101_interface_strobe(RF_SIDLE);
    cc1101_interface_strobe(RF_SFTX);

#ifdef FRAMEWORK_LOG_ENABLED // TODO more granular
    log_print_stack_string(LOG_STACK_PHY, "Data to TX Fifo:");
    log_print_data(packet->data, packet->length);
#endif

    configure_channel();

    cc1101_interface_write_burst_reg(TXFIFO, packet->data, packet->length); // tx_queue.front is length byte
    cc1101_interface_set_interrupts_enabled(true);
    cc1101_interface_strobe(RF_STX);
}
