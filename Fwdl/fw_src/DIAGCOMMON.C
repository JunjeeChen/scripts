/*******************************************************************************
*                                                                              *
*                     PROPRIETARY RIGHTS NOTICE:                               *
*                                                                              *
*   ALL RIGHTS RESERVED. THIS MATERIAL CONTAINS THE VALUABLE                   *
*                    PROPERTIES AND TRADE SECRETS OF                           *
*                                                                              *
*                            LANDIS+GYR                                        *
*                       ALPHARETTA, GA., USA,                                  *
*                                                                              *
*   EMBODYING SUBSTANTIAL CREATIVE EFFORTS AND CONFIDENTIAL INFORMATION,       *
*   IDEAS AND EXPRESSIONS, NO PART OF WHICH MAY BE REPRODUCED OR TRANSMITTED   *
*   IN ANY FORM OR BY ANY MEANS ELECTRONIC, MECHANICAL, OR OTHERWISE,          *
*   INCLUDING PHOTOCOPYING AND RECORDING OR IN CONNECTION WITH ANY             *
*   INFORMATION STORAGE OR RETRIEVAL SYSTEM WITHOUT THE PERMISSION IN          *
*   WRITING FROM:                                                              *
*                            LANDIS+GYR                                        *
*                                                                              *
*                          COPYRIGHT 2011                                      *
*                            LANDIS+GYR                                        *
*                                                                              *
*******************************************************************************/

/*******************************************************************************
           Copyright (C) 2011 Landis+Gyr
         Contains CONFIDENTIAL and TRADE SECRET INFORMATION
********************************************************************************


      Module Name:      diag.c

      Date:             December 1st, 2011

      Engineer:         John Bettendorff

      Description:      Diagnostic mode

                        This is the hardware specific diagnostic task functions.

                        This should primarily be a support file for the common
                        diagnostic task code.

                        There is some hooks for device specific commands. Not
                        all commands will make sense for all products so
                        to keep the main code clean, there will be some hooks
                        over hear.

                        But remember, the long term goal is to keep diagnostic
                        mode the same for all products.

      Changes:
         12/01/11 JAB   Created

****************************LANDIS+GYR CONFIDENTIAL****************************/

#include "std.h"
#include "D_COMMON.H"
#include "crc.h"
#include "nvtag.h"
#include "../4g/l4g_rf.h"
#include "../4g/l4g.h"
#include "../4e/l4e.h"
#include "../4e/l4e_mlde.h"
#include "../4e/l4e_mib.h"
#include "../4e/l4e_mlsm.h"
#include "hal_timer.h"
#include "RegCode.h"
#include "transpar.h"
#include "stat_ram.h"
#include "event.h"
#include "s_flash.h"
#include "mclock.h"
#include "protect.h"

#ifdef PSEMX_SUPPORTED
#include "psemx.h"
#endif

#ifdef SERIES5
#include "sx1233.h"
#endif

#ifdef ELSTER_REX4
#include "Si4467.h"
#endif

#if defined(S5_EV_LCE) || defined(SERIES5_JP) || defined(SERIES5_JP_EP) || defined(S5_TOSHIBA_AP)
#include "CC1200.h"
#include "zigbee.h"
#endif

#if defined(USE_PANA) && !defined(REGION_JAPAN)
#include "PANAKey.h"
#endif

#ifdef METER_REXU_SUPPORTED
#include "driver.h"
#endif

#ifdef WISUN_FAN_SUPPORT
 #include "l4e_csma.h"
#endif

#define FWDL_UART_SUPPORT

#ifdef FWDL_UART_SUPPORT
   #include "nmp.h"

   #define FWDL_UART_ReadBuff      debug_in_buffer
   #define FWDL_UART_WriteBuff     debug_out_buffer
   #define FWDL_UART_FlushOut      flush_debug_out
   
   #define FWDL_UART_HDR_LEN       10
   #define FWDL_UART_MAX_LEN       (FWDL_UART_HDR_LEN + 256)

   #define FWDL_START_BYTE         0xEF

   #define FWDL_UART_FIRST_PKT     (0x01)
   #define FWDL_UART_LAST_PKT      (0x02)

   #define FWDL_UART_OK            (0x00)
   #define FWDL_UART_BAD_START     (0x01)
   #define FWDL_UART_BAD_LEN       (0x02)
   #define FWDL_UART_BAD_CRC       (0x03)
   #define FWDL_UART_BAD_IMG       (0x04)

   typedef struct 
   {
      uint8_t   fwdl_start;
      uint8_t   flag; //bit_7~0: unused; bit_1: set if the last pkt; bit_0: set if the first pkt
      uint32_t  offset;
      uint16_t  data_len;
      uint16_t  crc;
   }FWDL_UART_Hdr;

   unsigned char FWDL_UART_WaitForPacket(unsigned char *pkt_buf, FWDL_UART_Hdr * hdr);
   unsigned int FWDL_UART_GetBufferTimed(unsigned char *rx_buff, unsigned int data_len);
   
   extern unsigned char verifyFWDLPartNumber(unsigned char *part_num);
   
   extern void SetMeterDownloadActive(unsigned char new_state);
#endif

#define RESET_TEST            1
#define RF_MAX_CHANNEL 128
#define RECEIVE_PING  1
#define RECEIVE_ACK   2
#define TX_DELAY_PERIOD     2000    // microseconds
uint8_t        s_aTmpDiagBuffer[2048];
extern uint16_t  g_unLastFEI;

extern bool g_bcheckMACHDR;
bool listenBeforeTx = FALSE;
bool g_diagTemperatureCompensate = TRUE;
unsigned int last_rssi_read;

#ifdef MAC_PHY_SNIFF_SUPPORT
   unsigned char ucCSVFormat = 0;
#endif // MAC_PHY_SNIFF_SUPPORT

#ifdef WISUN_FAN_SUPPORT
#ifdef WISUN_FAN_SUPPORT_DEBUG
extern uint8_t  g_PhrData[]; //debug: report phy header, not compiled by default
#endif
#endif /* WISUN_FAN_SUPPORT */

uint8_t c_aMHRPingPlain[] = {
  0x61, 0xEC, 0x00,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
  'p','i','n','g'
};
/* The Ping ACK packet is using Data mode -- not ACK mode -- so L4E support
** is not required when parsing the 4g header.
*/
uint8_t c_aAck[] = {0x41, 0xEC, 0x0,
0,0,0,0,0,0,0,0,
0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0};

uint8_t c_aMHRPlain[] = {
  0x60, 0xE0, 0x00
};

#ifdef WISUN_PHY_CONF_TEST
const unsigned char pn9_511byte[511] =
{
    0xFF, 0x83, 0xDF, 0x17, 0x32, 0x09, 0x4E, 0xD1,
    0xE7, 0xCD, 0x8A, 0x91, 0xC6, 0xD5, 0xC4, 0xC4,
    0x40, 0x21, 0x18, 0x4E, 0x55, 0x86, 0xF4, 0xDC,
    0x8A, 0x15, 0xA7, 0xEC, 0x92, 0xDF, 0x93, 0x53,
    0x30, 0x18, 0xCA, 0x34, 0xBF, 0xA2, 0xC7, 0x59,
    0x67, 0x8F, 0xBA, 0x0D, 0x6D, 0xD8, 0x2D, 0x7D,
    0x54, 0x0A, 0x57, 0x97, 0x70, 0x39, 0xD2, 0x7A,
    0xEA, 0x24, 0x33, 0x85, 0xED, 0x9A, 0x1D, 0xE1,
    0xFF, 0x07, 0xBE, 0x2E, 0x64, 0x12, 0x9D, 0xA3,
    0xCF, 0x9B, 0x15, 0x23, 0x8D, 0xAB, 0x89, 0x88,
    0x80, 0x42, 0x30, 0x9C, 0xAB, 0x0D, 0xE9, 0xB9,
    0x14, 0x2B, 0x4F, 0xD9, 0x25, 0xBF, 0x26, 0xA6,
    0x60, 0x31, 0x94, 0x69, 0x7F, 0x45, 0x8E, 0xB2,
    0xCF, 0x1F, 0x74, 0x1A, 0xDB, 0xB0, 0x5A, 0xFA,
    0xA8, 0x14, 0xAF, 0x2E, 0xE0, 0x73, 0xA4, 0xF5,
    0xD4, 0x48, 0x67, 0x0B, 0xDB, 0x34, 0x3B, 0xC3,
    0xFE, 0x0F, 0x7C, 0x5C, 0xC8, 0x25, 0x3B, 0x47,
    0x9F, 0x36, 0x2A, 0x47, 0x1B, 0x57, 0x13, 0x11,
    0x00, 0x84, 0x61, 0x39, 0x56, 0x1B, 0xD3, 0x72,
    0x28, 0x56, 0x9F, 0xB2, 0x4B, 0x7E, 0x4D, 0x4C,
    0xC0, 0x63, 0x28, 0xD2, 0xFE, 0x8B, 0x1D, 0x65,
    0x9E, 0x3E, 0xE8, 0x35, 0xB7, 0x60, 0xB5, 0xF5,
    0x50, 0x29, 0x5E, 0x5D, 0xC0, 0xE7, 0x49, 0xEB,
    0xA8, 0x90, 0xCE, 0x17, 0xB6, 0x68, 0x77, 0x87,
    0xFC, 0x1E, 0xF8, 0xB9, 0x90, 0x4A, 0x76, 0x8F,
    0x3E, 0x6C, 0x54, 0x8E, 0x36, 0xAE, 0x26, 0x22,
    0x01, 0x08, 0xC2, 0x72, 0xAC, 0x37, 0xA6, 0xE4,
    0x50, 0xAD, 0x3F, 0x64, 0x96, 0xFC, 0x9A, 0x99,
    0x80, 0xC6, 0x51, 0xA5, 0xFD, 0x16, 0x3A, 0xCB,
    0x3C, 0x7D, 0xD0, 0x6B, 0x6E, 0xC1, 0x6B, 0xEA,
    0xA0, 0x52, 0xBC, 0xBB, 0x81, 0xCE, 0x93, 0xD7,
    0x51, 0x21, 0x9C, 0x2F, 0x6C, 0xD0, 0xEF, 0x0F,
    0xF8, 0x3D, 0xF1, 0x73, 0x20, 0x94, 0xED, 0x1E,
    0x7C, 0xD8, 0xA9, 0x1C, 0x6D, 0x5C, 0x4C, 0x44,
    0x02, 0x11, 0x84, 0xE5, 0x58, 0x6F, 0x4D, 0xC8,
    0xA1, 0x5A, 0x7E, 0xC9, 0x2D, 0xF9, 0x35, 0x33,
    0x01, 0x8C, 0xA3, 0x4B, 0xFA, 0x2C, 0x75, 0x96,
    0x78, 0xFB, 0xA0, 0xD6, 0xDD, 0x82, 0xD7, 0xD5,
    0x40, 0xA5, 0x79, 0x77, 0x03, 0x9D, 0x27, 0xAE,
    0xA2, 0x43, 0x38, 0x5E, 0xD9, 0xA1, 0xDE, 0x1F,
    0xF0, 0x7B, 0xE2, 0xE6, 0x41, 0x29, 0xDA, 0x3C,
    0xF9, 0xB1, 0x52, 0x38, 0xDA, 0xB8, 0x98, 0x88,
    0x04, 0x23, 0x09, 0xCA, 0xB0, 0xDE, 0x9B, 0x91,
    0x42, 0xB4, 0xFD, 0x92, 0x5B, 0xF2, 0x6A, 0x66,
    0x03, 0x19, 0x46, 0x97, 0xF4, 0x58, 0xEB, 0x2C,
    0xF1, 0xF7, 0x41, 0xAD, 0xBB, 0x05, 0xAF, 0xAA,
    0x81, 0x4A, 0xF2, 0xEE, 0x07, 0x3A, 0x4F, 0x5D,
    0x44, 0x86, 0x70, 0xBD, 0xB3, 0x43, 0xBC, 0x3F,
    0xE0, 0xF7, 0xC5, 0xCC, 0x82, 0x53, 0xB4, 0x79,
    0xF3, 0x62, 0xA4, 0x71, 0xB5, 0x71, 0x31, 0x10,
    0x08, 0x46, 0x13, 0x95, 0x61, 0xBD, 0x37, 0x22,
    0x85, 0x69, 0xFB, 0x24, 0xB7, 0xE4, 0xD4, 0xCC,
    0x06, 0x32, 0x8D, 0x2F, 0xE8, 0xB1, 0xD6, 0x59,
    0xE3, 0xEE, 0x83, 0x5B, 0x76, 0x0B, 0x5F, 0x55,
    0x02, 0x95, 0xE5, 0xDC, 0x0E, 0x74, 0x9E, 0xBA,
    0x89, 0x0C, 0xE1, 0x7B, 0x66, 0x87, 0x78, 0x7F,
    0xC1, 0xEF, 0x8B, 0x99, 0x04, 0xA7, 0x68, 0xF3,
    0xE6, 0xC5, 0x48, 0xE3, 0x6A, 0xE2, 0x62, 0x20,
    0x10, 0x8C, 0x27, 0x2A, 0xC3, 0x7A, 0x6E, 0x45,
    0x0A, 0xD3, 0xF6, 0x49, 0x6F, 0xC9, 0xA9, 0x98,
    0x0C, 0x65, 0x1A, 0x5F, 0xD1, 0x63, 0xAC, 0xB3,
    0xC7, 0xDD, 0x06, 0xB6, 0xEC, 0x16, 0xBE, 0xAA,
    0x05, 0x2B, 0xCB, 0xB8, 0x1C, 0xE9, 0x3D, 0x75,
    0x12, 0x19, 0xC2, 0xF6, 0xCD, 0x0E, 0xF0
};

const char WiSUN_test_vector[18] =
{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11,
};
#endif

#ifdef  MAC_PHY_SNIFF_SUPPORT
void sniffParseMacPhy( const L4G_RX_MSG *  pRxMsg, unsigned int targetPANID, unsigned int targetRouteBPANID, unsigned char frameDisplayFilter, unsigned char routeDisplayFilter );
#endif

void diagFlashMode(void);
void SecurityMode(void);
void setDefaultRFparam(void);
void diagTestmodeExecute (void);
unsigned int HRFDiagChangePALevel(unsigned int increase);
void UpdateIPv6PortAddress(char *str, uint16_t *src_port, uint16_t *dest_port, unsigned char *pIPv6);
void diagSecurityMode(void);
void printHexData( const unsigned char * p_pData, unsigned int p_unDataLen );
void rf_config4e( int deprecated );
void rf_displayRfBaudrate( void );
uint8_t rf_checkChannelNumber( uint16_t channelNumber );
void rf_displayChannel( uint32_t chNo );
void rf_displayChannelMask();
void rf_square_wave (void);
void rf_receive_ping(unsigned char p_ucType);
void rf_ping_to(void);

void getPersistBitmap( uint8_t * p_pBitmap, uint8_t temp );

#ifdef WISUN_FAN_SUPPORT
void rf_configWiSun( void );
const char   * const verboseString[VB_NUMBER] = {
   "OFF",
   "Normal",
#if defined(DEBUG_DIAG_TEST)
   "RF Registers",
#endif
   "Packets",
   "All Packets"
};
#else /* WISUN_FAN_SUPPORT */
const char   * const verboseString[VB_NUMBER] = {
   "OFF",
   "Normal",
#if defined(DEBUG_DIAG_TEST)
   "RF Registers",
#endif
   "Packets"
};
#endif /* WISUN_FAN_SUPPORT */

/*
** Put these few commands up top because they are also used by the DIAG stripped mode of operation
*/

#ifdef USE_HOT_KEY_DIAG
#include "../util/hotkey.h"

#define configure_TABLE(_XF, _XT, _XX, _XE)  \
   _XF( 'A', a2d_scales_offsets_func,   "A2D Scales and Offsets" )      \
   _XF( 'B', change_conf_bytes_func,    "Change Config Bytes" ) \
   _XF( 'C', crc_addr_func,             "CRC Adder" )   \
   _XF( 'D', cnf_l4e_conf_func,         "4e config (deprecated)" )      \
   _XF( 'E', dis_enc_func,              "Disable Encryption" )  \
   _XF( 'F', set_hes_ip_ports_ntp_func, "Set HES0/1 IP, HES Ports, NTP0/1 IP, HB values, and APN" )       \
   _XF( 'F', set_ip_addr_ports_func,    "Set IP Addresses and Ports" )  \
   _XF( 'G', tog_diag_override_func,    "Toggle DIAG override" )        \
   _XF( 'H', hw_type_func,              "Hardware Type" )       \
   _XF( 'L', lan_id_func,               "Change LAN ID" )       \
   _XF( 'M', rf_ch_mask_func,           "RF Chan Mask" )        \
   _XF( 'N', read_nvram_func,           "Read Non-Volatile Memory" )    \
   _XF( 'P', rf_power_func,             "RF Power" )    \
   _XF( 'R', res_parms_to_man_def_func, "Reset Parameters to Manufacturing Defaults" )       \
   _XF( 'S', sw_type_func,              "Soft ID" )     \
   _XF( 'T', tcxo_err_func,             "TCXO Error" )  \
   _XF( 'U', set_tz_func,               "Set Time Zone" )       \
   _XF( 'Y', calibrate_temp_func,       "Calibrate temperature" )       \
   _XF( 'Z', dump_man_conf_func,        "Dump Manufacturing Configuration" )       \
   _XE()

#define HDiagRfS5_TABLE(_XF, _XT, _XX, _XE)  \
   _XF( '0', tog_rf_filter_func,     "Toggle RF Filter" )       \
   _XF( '9', dump_all_rf_reg,        "Dump all RF Registers" )  \
   _XE()

#define HDiagRfTest_TABLE(_XF, _XT, _XX, _XE)        \
   _XF( '1', tog_ant_sel_func, "Toggle Antenna Selection" )     \
   _XF( '2', disp_init_sx1233_reg_func, "Display initialized SX1233 registers and changes" )       \
   _XF( '4', drf_l4e_conf_func, "4e config" )   \
   _XF( 'A', chnage_crc_seed_func, "Change CRC Seed" )  \
   _XF( 'B', turn_xmit_on_func, "Turn transmitters on" )        \
   _XF( 'D', res_parm_func, "Reset parameters" )        \
   _XF( 'E', rssi_test_func, "RSSI Test" )      \
   _XF( 'F', rf_baud_func, "Set RF Baud Rate" ) \
   _XF( 'H', rf_chan_func, "Set RF Tx/Rx Channel" )     \
   _XF( 'J', rf_reg_write_func, "Write RF register" )   \
   _XF( 'K', rf_reg_read_func, "Read RF register" )     \
   _XF( 'L', pll_test_func, "PLL Lock Test" )   \
   _XF( 'M', rx_pkt_send_ack_func, "Rx packet and send ACK" )   \
   _XF( 'N', test_spec_func, "Specify Number and Length and timing of test packets" )       \
   _XF( 'Q', tx_mark_space_func, "Transmit Mark(s)/Space(s)" )  \
   _XF( 'R', enter_rx_mode_func, "Enter Rx mode using current channel" )        \
   _XF( 'P', tx_pkt_to_func, "Tx packet to ... " )      \
   _XF( 'S', disp_curr_sett_func, "Display current settings" )  \
   _XF( 'T', send_test_pkt_func, "Send Test Packets" )  \
   _XF( 'V', toggle_verb_func, "Toggle Verbose mode ON/OFF" )   \
   _XF( 'Z', spec_tx_mode_func, "Specify Transmit Mode" )       \
   _XX(      HDiagRfS5_TABLE, "S5 Device Specific" )    \
   _XE()


#define DiagFlash_TABLE(_XF, _XT, _XX, _XE)  \
   _XF( 'B', flash_burn_in_test_func, "Flash Burn In Test"    ) \
   _XF( 'D', dump_page_func, "Dump Page"    )   \
   _XF( 'E', erase_fill_flash_page_func, "Erase/Fill Flash Pages" )     \
   _XF( 'P', change_a_byte_func, "Change a Byte" )      \
   _XF( 'A', epf_arm_func, "ARM EPF and Init if necessary" )    \
   _XF( 'C', epf_disArm_func, "DisARM EPF" )    \
   _XF( 'Q', epf_dump_current_ram_func, "Dump Current (RAM) EPF and Alarm Page"    )       \
   _XF( 'R', force_alarm_func, "Force Alarm"    )       \
   _XF( 'S', change_index_func, "Change Index for Interval Data Page"    )       \
   _XF( 'T', nvtag_disp_func, "Display NV Tag's or NV Tag Test Menu (AT25)"    )       \
   _XF( 'U', nvtag_write_func, "Write NV Tag"    )       \
   _XF( 'V', nvtag_del_func, "Delete NV Tag")       \
   _XE()

#define man_test_TABLE(_XF, _XT, _XX, _XE)       \
  _XF( 'A', read_anlg_chs_func, "Read Analog Channels" ) \
  _XF( 'C', backup_code_rprt_func, "Backup Code Report" ) \
  _XF( 'D', lpp_loopback_test_func, "LPP Loopback Test" ) \
  _XF( 'E', tp_handshake_test_func, "TP Handshake Test" ) \
  _XF( 'F', version_func, "Firmware Version" ) \
  _XF( 'G', din_state_test_func, "Digital Input State Testing" ) \
  _XF( 'H', dout_high_test_func, "Digital Output High Testing" ) \
  _XF( 'I', dout_low_func, "Digital Output Low Testing" ) \
  _XF( 'J', jumper_test_func, "Jumper Test" ) \
  _XF( 'L', toggle_hu_cap_func, "Toggle Holdup Capacitor" ) \
  _XF( 'O', out_clock_func, "Output clock" ) \
  _XF( 'P', pwm_prime_ch_func, "Pwm primary channel ON" ) \
  _XF( 'R', pwm_scnd_ch_func, "Pwm secondary channel ON" ) \
  _XF( 'S', pwm_off_func, "Pwm OFF" ) \
  _XF( 'T', toggle_cap_charge_func, "Toggle Capacitor Charging" ) \
  _XF( 'U', toggle_cap_discharge_func, "Toggle Capacitor Discharging" ) \
  _XF( 'V', toggle_led_A_func, "Toggle LED A" ) \
  _XF( 'W', toggle_led_B_func, "Toggle LED B" ) \
  _XF( 'Z', zb_test_func, "ZigBee Test" )       \
  _XE()

#ifdef    OS_CHECKSTACK
#define OS_CHECKSTACK_FUNC_ENT( _XF )    _XF( '0', os_stack_func,    "Stack info" )
#else  /* OS_CHECKSTACK */
#define OS_CHECKSTACK_FUNC_ENT( _XF )
#endif /* OS_CHECKSTACK */

#define ROOT_TABLE(_XF, _XT, _XX, _XE)  \
    OS_CHECKSTACK_FUNC_ENT( _XF )       \
  _XF( 'A', dsr_toggle_func,    "Toggle DSR" )  \
  _XF( 'B', cts_toggle_func,    "Toggle CTS" )  \
  _XT( 'C', configure_TABLE,    "Configure" )   \
  _XT( 'F', DiagFlash_TABLE,    "Flash" )       \
  _XF( 'I', disp_events_func,   "Display Events" )      \
  _XT( 'M', man_test_TABLE,     "Manufacturing Test" )  \
  _XT( 'R', HDiagRfTest_TABLE,  "RF Test" )     \
  _XF( 'T', set_br_38400_func,  "Set baud rate to 38400" )      \
  _XF( 'Z', reboot_func,        "REBOOT" )      \
  _XE()

configure_TABLE  (HK_P_FUNCT, HK_P_TABLE, HK_P_XTBLE, HK_P_EMPTY)
HDiagRfS5_TABLE  (HK_P_FUNCT, HK_P_TABLE, HK_P_XTBLE, HK_P_EMPTY)
HDiagRfTest_TABLE(HK_P_FUNCT, HK_P_TABLE, HK_P_XTBLE, HK_P_EMPTY)
DiagFlash_TABLE  (HK_P_FUNCT, HK_P_TABLE, HK_P_XTBLE, HK_P_EMPTY)
man_test_TABLE   (HK_P_FUNCT, HK_P_TABLE, HK_P_XTBLE, HK_P_EMPTY)
ROOT_TABLE       (HK_P_FUNCT, HK_P_TABLE, HK_P_XTBLE, HK_P_EMPTY)

configure_TABLE  (HK_D_FUNCT, HK_D_TABLE, HK_D_XTBLE, HK_D_EMPTY)
HDiagRfS5_TABLE  (HK_D_FUNCT, HK_D_TABLE, HK_D_XTBLE, HK_D_EMPTY)
HDiagRfTest_TABLE(HK_D_FUNCT, HK_D_TABLE, HK_D_XTBLE, HK_D_EMPTY)
DiagFlash_TABLE  (HK_D_FUNCT, HK_D_TABLE, HK_D_XTBLE, HK_D_EMPTY)
man_test_TABLE   (HK_D_FUNCT, HK_D_TABLE, HK_D_XTBLE, HK_D_EMPTY)
ROOT_TABLE       (HK_D_FUNCT, HK_D_TABLE, HK_D_XTBLE, HK_D_EMPTY)

HK_ROOT_WITH_CONTEXT( ROOT_TABLE, diagHKcontext, "Diagnostic" )

static int DCDiagGetString(unsigned char *chPtr, unsigned int limit)
{
   unsigned int index = 0;

   for( index=0; DiagHit() && (index < limit) ; index++ )
   {
    chPtr[index] = DiagIn ();
    Pend (0, 10);
   }

   return (index);
}


#endif /* USE_HOT_KEY_DIAG */

void notSupported()
{
   DiagPrintf("Not supported\n");
}

#ifdef CELL_SBS
unsigned int GetString(unsigned char *Buffer, unsigned int BufferSize, unsigned char flag)
{
    unsigned int Index = 0;

    memset(Buffer, 0, BufferSize);
    do
    {
       Buffer[Index] = DiagIn();
       if ((Buffer[Index] == 0x0D) ||
           (Buffer[Index] == 0x0A) ||
           (Buffer[Index] == ESCAPE_CHAR))
       { // if got the end-of-line or escape character
         Buffer[Index] = 0;
         break;
       }
       if (flag)
       {
         DiagOut(Buffer[Index]);
       }
       if (Buffer[Index] == '\b')
       {
         Buffer[Index] = 0;  // remove backspace
         if (Index)
         {
           Index--;
         }
         Buffer[Index] = 0;  // remove previous character
         if (Index)
         {
           Index--;
         }
         if (flag)
         {
           DiagOut(' ');
           DiagOut('\b');
         }
       }
       if (Index == (BufferSize - 1))
       { // if filled up buffer
         Index++;
         Buffer[Index] = 0;
         break;
       }
    } while (Index++ < BufferSize);
    return (Index);
}
#endif // CELL_SBS

unsigned int
DCDiagGetMenuCommand (const unsigned char *menu)
{
   unsigned int c;

   DiagPrintf("\n%s\n\n[Hit '?' for help]: ", menu);

   while (!DiagHit ())
   {
      Pend (0, 10);

#ifdef S5_EV_LCE
      if(!BUTTON_STATE)
      {
        setDefaultRFparam();
        DiagPrintf ("Diagnostic Test Mode started...\nAgain Press Push Button to execute Test Modes\n");
        while(1)
            {
                Pend(0,10);

                if (!BUTTON_STATE)
                    diagTestmodeExecute();
            }
      }
#endif //S5_EV_LCE

   }
   c = DiagIn ();

   if (c > '9')
      c &= 0xdf;        // Convert to upper case

   if (c < '0')
      DiagPrintf ("\n");
   else
      DiagPrintf ("%c\n", c);

   return (c);
}

#ifdef DIAG_STRIPPED
/*
** If DIAG_STRIPPED is enabled, this pretty much means that we are building this version for a
** device that is out of production and we want the code space used by diagnostic mode.
**
** In that case, provide the bare minimum. A few configuration options and that's it.
**
** This is a task, so make the user reboot to get back out of it.
*/
void DiagnosticTask(void)
{
   int c;

   while (1)
      {
      c = DCDiagGetMenuCommand ("Diagnostic (Stripped) Mode");

      switch(c)
         {
         case 'H':
            c = (unsigned char) DCDiagGetInt32 ("Enter Hardware Type (0-255): ",
                                     0, 255, HFCData.MinorHardwareType);
               /* don't update anything unless the user changed the hardware type */
            if (c != HFCData.MinorHardwareType)
               {
               HFCData.MinorHardwareType = c;
               ResetFC(HFCData.MinorHardwareType);

                  /*
                  ** Make sure defaults are correct for this radio type.
                  ** But don't lose a factory loaded DCW
                  */
               eeGetandValidate(EE_RESET_FIRMWARE);
               }
            break;

         case 'L':
            DiagPrintf ("Enter HEX Byte 1: ");
            HFCData.LAN[0] = (unsigned char) DCDiagGetHexString (2, HFCData.LAN[0]);
            DiagPrintf ("Enter HEX Byte 2: ");
            HFCData.LAN[1] = (unsigned char) DCDiagGetHexString (2, HFCData.LAN[1]);
            DiagPrintf ("Enter HEX Byte 3: ");
            HFCData.LAN[2] = (unsigned char) DCDiagGetHexString (2, HFCData.LAN[2]);
            DiagPrintf ("Enter HEX Byte 4: ");
            HFCData.LAN[3] = (unsigned char) DCDiagGetHexString (2, HFCData.LAN[3]);

            memcpy (&RamLanSrc_M, &HFCData.LAN, sizeof(HFCData.LAN));

               /*
               ** Now that we have a LAN address, assuming it is not 0, set
               ** the operational bit so the radio will run.
               */
            if (get32 ((unsigned char *)&RamLanSrc_M))
               FirstConfigByte_M |= OPERATIONAL;

               /*
               ** Write the LAN address to the serial emulation block and to the
               ** factory constants block.
               */
            eeUpdateImage();
            WriteFC();
            break;

         case 'R':
               /*
               ** Keep this one different than the hardware ID version. Here we do want to knock out the
               ** DCW if someone does run this.
               */
            eeGetandValidate(EE_RESET_SHOP);
            break;

         default:
            DiagPrintf ("Choose one of the following commands\n"
                  "  H - Hardware Type\n"
                  "  L - Change LAN ID\n"
                  "  R - Reset Parameters to Manufacturing Defaults\n"
                  "  ? - Displays this message\n");
            break;
         }
      }

} /* End of DiagnosticTask */

#else

/*
** The stripped mode doesn't need all these variables, so declare them down here.
*/
unsigned char           *diagBuffer;// = &s_aTmpDiagBuffer[0]; //TODO: TempTest VP
unsigned int            rfTestBaud;
unsigned int            rfTestChannel;
unsigned int            rfTestCrcSeed;
unsigned int            rfTestLockChannel;
unsigned int            rfTestLockDelay;
unsigned int            rfTestLockFails;
unsigned int            rfTestLockMax;
unsigned int            rfTestLockSamples;
unsigned long           rfTestLockTotalTime;
unsigned int            rfTestPacketNumber;
unsigned int            rfTestRxPackets;
unsigned int            rfTestTxDelay;
unsigned int            rfTestTxSent;

unsigned int            rfTestCharacter;
unsigned char           rfTestData;
unsigned char           rfTestDataAck;
unsigned char           rfTestDataType;
unsigned int            rfTestPacketLength;
unsigned char           rfTestStrlen;
unsigned char           rfTestString[16];
unsigned char           rftest_check4eHeader;
VerboseType             rfTestVerbose;

extern uint32_t g_unLastChannel;
extern CurrentRfMode_t g_stLastRfModeIdx;
#ifdef WISUN_FAN_SUPPORT
extern uint8_t g_unPhyFSKPreambleLength; //for configurable preamble length
#endif

CurrentRfMode_t s_stSelectedTestMode;
uint32_t s_unAckMode;

void	diagSendTest (unsigned char response);

#ifdef   DIAGNOSTIC_SELFTEST
   #define  SELFTEST_OFF            (0)
   #define  SELFTEST_START          (1)
   #define  SELFTEST_LIMIT          (9)
   #define  CHANNEL_LIMIT           (6)
   #define  BAUDTEST_LIMIT          (6)

   unsigned char  selftest_state;
   unsigned char  baudtest_state;
   unsigned char  button_state;
   unsigned long  button_last_stable;
   unsigned char  diag_j_state;
   unsigned long  diag_j_last_stable;

      /*
      ** Define the different commands we will step through. The \033 is the escape character
      ** A higher priority task will place this data into the console port where the normal routines
      ** will then pull out the data.
      */
   const char   * const selftest_commands [SELFTEST_LIMIT] = {
   "ZRNT",
   "T",
   "T",
   "QFE\r",
   "QFE\r",
   "QFE\r",
   "ZRAR",
   "R",
   "R",
   };

   const char   * const channel_commands [CHANNEL_LIMIT] = {
   "\033H1\r",
   "\033H130\r",
   "\033H259\r",
   "\033H2\r",
   "\033H130\r",
   "\033H258\r",
   };

   const char   * const baudtest_commands [BAUDTEST_LIMIT] = {
   "\033F9600\rN0\r24\r",
   "\033F19200\rY",
   "\033F19200\rN",
   "\033F38400\rN0\r248\r",
   "\033F115200\r",
   };

unsigned char
diagSelftestActive (void)
{
      /*
      ** If DIAG jumper and button are both pushed then we can go into special DIAG mode
      */
   if ((Jumpers() & JUMPER_1) && !BUTTON_STATE)
      return (TRUE);
   else
      return (FALSE);
}

void
diagCheckSelftest (void)
{
   unsigned char latch;

      /* Don't do this if we aren't in selftest mode */
   if (selftest_state)
      {
      latch = !BUTTON_STATE;
      if (button_state != latch)
         {
         if ((SysTimeGet() - button_last_stable) > 125UL)
            {
            button_state = latch;
            button_last_stable = SysTimeGet();

               /* When asserted, pump out a new command */
            if (button_state)
               {
               unsigned char index = (selftest_state - 1) % 3;

               if (rfTestBaud > RF_BAUD_38400)
                  index += 3;

               debugForceInput (channel_commands[index]);
               debugForceInput (selftest_commands[selftest_state - 1]);
               if (++selftest_state > SELFTEST_LIMIT)
                  selftest_state = SELFTEST_START;
               }
            }
         }
      else
         {
         button_last_stable = SysTimeGet();
         }

      latch = (Jumpers() & JUMPER_1);
      if (diag_j_state != latch)
         {
         if ((SysTimeGet() - diag_j_last_stable) > 125UL)
            {
            diag_j_state = latch;
            diag_j_last_stable = SysTimeGet();

               /* When asserted, pump out a new command */
            if (diag_j_state)
               {
               if (++baudtest_state >= BAUDTEST_LIMIT)
                  baudtest_state = 0;

               debugForceInput (baudtest_commands[baudtest_state]);

               selftest_state = SELFTEST_START;
               }
            }
         }
      else
         {
         diag_j_last_stable = SysTimeGet();
         }
      }
   else
      {
         /*
         ** If we aren't in selftest mode but we see the condition, then force a character into the
         ** input stream to get us into the mode. Note this only works if we are at the main diagnostic
         ** prompt.
         */
      if (diagnosticActive && diagSelftestActive())
         debugForceInput ("\033");
      }
}
#endif //DIAGNOSTIC_SELFTEST

void
diagBumpVerboseMode (void)
{
   rfTestVerbose = (VerboseType)((rfTestVerbose+1) % VB_NUMBER);
   DiagPrintf ("Verbose is %s\n", verboseString[rfTestVerbose]);
}

/*
   diagRFRuntimeCommands

   Process any commands that happen while we are doing a test. The
   character that has been entered is returned, 0 is returned if
   no character was entered. So far the following commands are
   processed:

      ESC      Indicates the test should be stopped
      R     Print # of packets received
      T     Print # of packets left to be sent
      V     Toggle verbose mode
      +     Bump channel up by 1
      -     Bump channel down by 1
*/
int
diagRFRuntimeCommands (char who)
{
   int c = 0;
   int dec, frac;

   while (DiagHit ())
      {
      switch ((c = DiagIn ()))
         {
         case 'x':
         case 'X':
            //Quit on an escape character
         case ESCAPE_CHAR:
            return ESCAPE_CHAR;

         case 'b':
            DiagPrintf ("%u\n", HRFPortCrcError);
            break;
         case 'g':
            DiagPrintf ("%u\n", rfTestRxPackets);
            break;

         case 'B':
         case 'G':
            if (rfTestRxPackets + HRFPortCrcError)
               {
               dec = (int) ((HRFPortCrcError * 100UL) / (rfTestRxPackets + HRFPortCrcError));
               frac = (int) (((HRFPortCrcError * 100UL) % (rfTestRxPackets + HRFPortCrcError)
                           *1000L + ((rfTestRxPackets + HRFPortCrcError) >> 1)) /
                          (rfTestRxPackets + HRFPortCrcError));
               }
            else
               dec   = frac = 0;

            if ((rfTestDataAck == 'A') && (who == 'T'))
               DiagPrintf ("Tx: %5u  ", rfTestTxSent);

            DiagPrintf ("Rx: %5u  CRC Errors: %5u  Error: %01u.%03u%%\n",
                  rfTestRxPackets, HRFPortCrcError, dec, frac);
            break;

         case 'U':
            c = 'I'; //Japan uses 'U' (Up) within RF Mode commands to increment Power Level
         case 'd':
         case 'D':
         case 'i':
         case 'I':
            DiagPrintf("\nRF Power level %u now using %u\n", RamMaxPower_M, HRFDiagChangePALevel (((c & 0xdf) == 'I') ? TRUE : FALSE));
            return (RESET_TEST);
         case 'l':
         case 'L':
            if ((who & 0xdf) == 'L')
               {
                  if (who == 'L')
                  DiagPrintf ("Working on Channel %u.%u Mhz, ", 902 + rfTestLockChannel / 10, rfTestLockChannel % 10);
				  DiagPrintf ("Max lock time is %u uSec\n", rfTestLockMax);
               }
            break;

         case 'p':
         case 'P':
               /* Decrement power level each user request. Let it wrap to big number, rfPower will fix it */
            RamMaxPower_M = rfPower(RamMaxPower_M - 1);
            DiagPrintf("\nRF User Power level is now %u\n", RamMaxPower_M);
            return (RESET_TEST);

         case 'r':
         case 'R':
            HRFPortCrcError = 0;
            rfTestRxPackets = 0;
            break;

         case 's':
         case 'S':
            if (who == 'R')
            {
               L4G_AbortCrtAction();
               dec = L4G_CheckNoise(s_stSelectedTestMode);
               L4G_StartRx( s_stSelectedTestMode, 0, 0 );
               if ((mh_only == MH_EVLCE_INDIA) || (mh_only == MH_GENERAL_U_ANZ)|| (mh_only == MH_GENERAL_S1100_AP))
               {
                  DiagPrintf("RSSI: %02x (-%d dBm)\n", dec, dec);
               }
               else
               {
                  /* Reads Carrier Sense or RSSI Threshold Register on the radio chip */
                DiagPrintf ("RSSI: -%3d dBm, Noise_Threshold: -%d dBm\n", dec, L4G_RF_GetNoiseThreshold());
               }
            }
            break;
         case 'n':
         case 'N':
#if defined(S5_EV_LCE) || defined(S5_TOSHIBA_AP)
            if ((mh_only == MH_EVLCE_INDIA) || (mh_only == MH_GENERAL_U_ANZ) || (mh_only == MH_GENERAL_S1100_AP))
            {
               if (who == 'R')
               {  /* This special runtime command is for Justin's rx sensitivity testing/debug where we self induce noise through dummy spi reads before reading actual RSSI.
                  ** Specific to CC1200.  Is this a one-time or on-going investigation for ANZ?
                  */
                  #define NOISE_DURATION 100  //ms
                  uint32_t ulStartTime = OS_GetTime32();
                  while(OS_GetTime32() - ulStartTime < NOISE_DURATION)
                  {
                    MicroPend(10); // no hogging, need to feed the watchdog
                    CC1200GetRnd();
                  }

                  dec = L4G_CheckNoise(s_stSelectedTestMode);


                  DiagPrintf("NRSSI: %02x (-%d dBm)\n", dec, dec);
               }
            }
#endif //S5_EV_LCE
            break;

         case 't':
         case 'T':
            DiagPrintf ("Packets Sent: %u\n", rfTestTxSent);
            break;

         case 'v':
         case 'V':
            diagBumpVerboseMode();
            break;

         case '+':
            L4G_RF_SetChannel( (g_unLastChannel + 1) % RF_MAX_CHANNEL );
            rf_displayChannel(g_unLastChannel);
            return (RESET_TEST);

         case '-':
            L4G_RF_SetChannel( (g_unLastChannel + RF_MAX_CHANNEL - 1) % RF_MAX_CHANNEL );
            rf_displayChannel(g_unLastChannel);
            return (RESET_TEST);

         default:
            DiagPrintf ("Available commands\n"
                  "        b  Display bad packets (classic style)\n"
                  "        g  Display good packets (classic style)\n"
                  "      B/G  Display received packet summary\n"
                  "      l/L  Display maximum PLL lock time (lock test only)\n"
                  "      d/D  Decrease RF Power PA Level\n"
                  "    i/I/U  Increase RF Power PA Level\n"
                  "      p/P  Decrease RF Power User Level\n"
                  "      r/R  Reset packet counters\n"
                  "      s/S  Read current RSSI (RX mode only)\n"
                  "      t/T  Display packets transmitted\n"
                  "      v/V  Toggle verbose mode\n"
                  "        +  Move to next channel\n"
                  "        -  Move to previous channel\n"
                  "x/X/esc -  Exit\n");
            break;
      }
   }

   return(c);
}

#if !defined(CELL_SBS)

/*
** diagTransmitterTest ()  Command B
**
** Allow user to turn RF transmitter on. This can be used to put out CW or a bit pattern.
** If cw_mode is TRUE, it will put out CW on the center of the frequency
** Othewise it will prompt for a pattern. Depending on what the user enters, this is what you will get
**    00 - This will put out CW on the low edge of the deviation
**    FF - This will put out CW on the high edge of the deviation
**    55 - This will put out a patern of alternating bits
**    FE - This will put out random data
*/
void
diagTransmitterTest (void)
{
    unsigned int tempInt;
    unsigned long stopTime;
    unsigned int unCrtTxCh = 0xFFFFFFFF;
    unsigned int cmd;

        // Send a single packet to take care of frequency offset drift the first time this 'B' test is run. CC1200 Freq is off by 25K.
    L4G_SendPacket( L4G_GetCurrentRfMode( s_stSelectedTestMode.stOpMode.bStartMode
                                          , s_stSelectedTestMode.stOpMode.bStartMode
                                          , 0, 0, 0
                                          , RamMaxPower_M )
                    , HalTimerGetValueUs(TIMER_L4G) + TX_DELAY_PERIOD
                    , s_aTmpDiagBuffer
                    , 1 );

    tempInt = (unsigned int) DCDiagGetInt32 (
                  "Leave RF transmitter on for how many seconds (0 is forever) [0 - 60]: ",
                  0, 60, 10);

#ifdef WISUN_PHY_CONF_TEST
    uint8_t  len = 64;
    uint16_t index = 0;
#endif // WISUN_PHY_CONF_TEST

    DiagPrintf("Transmitter is on, hit ESC to abort\n");

    stopTime = SysTimeGet() + (tempInt * 1000L);

    while( (cmd = diagRFRuntimeCommands('B')) != ESCAPE_CHAR )
    {
        // Change channel if user hit '+' or '-'
        if( (unCrtTxCh != g_unLastChannel) || (cmd == RESET_TEST) )
        {
            unCrtTxCh = g_unLastChannel;
            L4G_SendPacket( L4G_GetCurrentRfMode( s_stSelectedTestMode.stOpMode.bStartMode
                                                , s_stSelectedTestMode.stOpMode.bStartMode
                                                , 0, 0, 0
                                                , RamMaxPower_M )
                          , HalTimerGetValueUs(TIMER_L4G) + 100  //Must be short to work with CW Tranmit.  Use 100 to cover L4G response time.
                          , s_aTmpDiagBuffer
                          , 1024 );

            DiagPrintf("TX On Pwr %d  Reg %d\n",RamMaxPower_M,L4G_RF_GetPAPower());

               /* Zero Deviation after Tx Start which updates Deviation according to Bit Rate. */
            L4G_RF_InitiateCWTransmit();
        }
#ifndef WISUN_PHY_CONF_TEST
        L4G_ReloadTxContinous(s_aTmpDiagBuffer, 64);
#else
        L4G_ReloadTxContinous( &pn9_511byte[index], len );

        index += len;

        if( index == 448 )
        {
            len = 63;
        }
        else if( index == 511 )
        {
            index = 0;
            len = 64;
        }
        else
        {
            len = 64;
        }
#endif // WISUN_PHY_CONF_TEST

         if (tempInt && ((long)(SysTimeGet() - stopTime) > 0))
            break;
        Pend( 0, 1 ); // to feed the watchdog
    }

    L4G_AbortCrtAction();
    L4G_RF_RetireCWTransmit(L4G_GetCurrentRfMode(s_stSelectedTestMode.stOpMode.bStartMode,
                                                 0, 0, 0, 0, RamMaxPower_M));   //Restoral of Deviation
    DiagPrintf("Transmitter is off\n");
}


/*
** diagReceiveTest
**
** This routine either waits forever for an unlimited number of packets
** or it waits a short period of time for one packet.
*/
void
diagReceiveTest (unsigned long p_ulExpectedPkHdr )
{
   unsigned long start_time;
   unsigned int unCrtTxCh = 0xFFFFFFFF;
   OS_TASK_EVENT ucCrtEvent;
   unsigned long usRxCounter = 0;
  char LastButtonEdge = 1;
#ifdef MAC_PHY_SNIFF_SUPPORT
   unsigned int targetPANID = 0;
   unsigned int targetRouteBPANID = 0;
   unsigned char frameDisplayFilter = 0;
   unsigned char routeDisplayFilter = 0;
#endif // MAC_PHY_SNIFF_SUPPORT

#ifdef MAC_PHY_SNIFF_SUPPORT
   DiagPrintf("\n enter target Route A PANID (hex)");
   targetPANID = (unsigned int) DCDiagGetHexString (4, 0x0000);
#ifdef ROUTE_B
   DiagPrintf("\n enter target Route B PANID (hex)");
   targetRouteBPANID = (unsigned int) DCDiagGetHexString (4, 0x0000);
#else
   targetRouteBPANID = 0x0000;
#endif
   DiagPrintf("\n 0-display all frames");
   DiagPrintf("\n 1-display only data frames");
   DiagPrintf("\n 2-display only ack frames");
   DiagPrintf("\n 3-display all excluding beacons");
   DiagPrintf("\n 4-display only beacon frames");
   DiagPrintf("\n 5-display only cmd frames");

   frameDisplayFilter = (unsigned char) DCDiagGetInt32 ( "\nEnter frame display mask[0-5]: ", 0, 5, frameDisplayFilter );

#ifdef ROUTE_B
   DiagPrintf("\n 0-display routes A and B");
   DiagPrintf("\n 1-display only route A");
   DiagPrintf("\n 2-display only route B");

   routeDisplayFilter = (unsigned char) DCDiagGetInt32 ( "\nEnter route display mask[0-2]: ", 0, 2, routeDisplayFilter );
#else
   routeDisplayFilter = 0;
#endif

   DiagPrintf("\n 0-human readable format");
   DiagPrintf("\n 1-CSV format");

   ucCSVFormat = (unsigned char) DCDiagGetInt32 ( "\nEnter format[0-1]: ", 0, 1, ucCSVFormat );
#endif // MAC_PHY_SNIFF_SUPPORT

   // Don't output this "diagReceiveTest started" message when expecting an ACK response.
   if (rfTestDataAck != 'A')
#ifndef MAC_PHY_SNIFF_SUPPORT
      DiagPrintf("diagReceiveTest started\n");
#endif

   if(!p_ulExpectedPkHdr)
      {
      HRFPortCrcError = 0;
      rfTestRxPackets = 0;

#ifndef MAC_PHY_SNIFF_SUPPORT
        DiagPrintf ("waiting for rf data\n");
#endif
      }

#ifdef MAC_PHY_SNIFF_SUPPORT
   if( ucCSVFormat )
   {
     DiagPrintf ("\ntime,route,MS,FCS,DW,length,sequence number,RSSI,LQI,frame type,security enabled?,frame pending?,ack reqd,PANID comp,seq num supp?,IE list pres?,dest addr mode,frame ver,src addr mode,src addr,dest addr,ASN,ACK TCIE");
   }
#endif

   L4G_GetLastRxMsg(); // discard RX pending message
   OS_ClearEvents( &TCB_L4E_Task ); // clear all pending events

   start_time = SysTimeGet();

  #ifndef COM_ADAPTER
   g_bcheckMACHDR = FALSE;
  #endif // COM_ADAPTER

   while( 1 )
   {
      if (unCrtTxCh != g_unLastChannel)
      {
         unCrtTxCh = g_unLastChannel;
#ifdef SERIES5
         if(!p_ulExpectedPkHdr)
         {
            for (int i=0;i<5;i++) L4G_RF_MeasureNoise();  //Insure SX1233 BG Noise has smoothed
         }
#endif
         if( p_ulExpectedPkHdr && s_unAckMode )
         {
             L4G_StartRx( L4G_GetCurrentRfMode(s_stSelectedTestMode.stOpMode.bNextMode,
                                               0, 0, 0, 0, 0),
                          0, 0 );
         }
         else
         {
             L4G_StartRx( s_stSelectedTestMode, 0, 0 );
         }
      }
         /* Packet Times: 1536 bytes @ 50 Kbaud = 245.7 mS.  512 bytes @ 50 Kbaud = 81.9 mS */
      ucCrtEvent = OS_WaitEventTimed( 0xFF, 98 );  //Not an exact multiple of 300 mS Tx Delay

      if( ucCrtEvent & L4E_EVENT_RX_PHR )
      {
          if( rfTestVerbose == VB_PACKET )
          {
#ifndef MAC_PHY_SNIFF_SUPPORT
              DiagPrintf("\tRX SFD: FEI:%d Hz, LNA %d\n", (signed int)((int16_t)g_unLastFEI * 5722 / 100),(int8_t)g_stRxMsg.m_ucLNA );  //SX1233 LNA is 0 to 6 range step.  CC1200 LNA is +63 to -64 in dB.
#endif
          }
          usRxCounter = 0;
          ucCrtEvent = OS_WaitEventTimed( L4E_EVENT_RX_MSG | L4E_EVENT_RX_BAD_MSG, g_stTSTemplate.m_unMaxWaitEndMsg_Ms );
      }

      const L4G_RX_MSG *  pstRxMsg = L4G_GetLastRxMsg();  //This can return a NULL
      if( ucCrtEvent & L4E_EVENT_RX_BAD_MSG )  //Bad Message has higher priority then Message Done
      {
          #ifdef WISUN_FAN_SUPPORT
          if(rfTestVerbose == VB_RAW_PKT)
          {
               DiagPrintf("BAD MSG\n");
               //Data
               printHexData( g_stRxMsg.m_ucData /*+ 2*/, g_stRxMsg.m_unRxDataLength /*- 2*/ );
               //PHY header
               #ifdef WISUN_FAN_SUPPORT_DEBUG
               DiagPrintf("RX PHY header on RF 0x%04X\n", Lget16(&g_PhrData[0]));
               #endif
               DiagPrintf("RX PHY header 0x%04X\n", Lget16(&g_stRxMsg.m_ucData[0]));
               L4G_STD_PHR* phr = (L4G_STD_PHR*)g_stRxMsg.m_ucData;
               DiagPrintf("RX PHR.MS Flag:0x%X; PHR.FCS:%d bytes, PHR.Whitening:0x%X, PHR.Len:%d bytes\r", phr->m_bMSFlag, (phr->m_bFCS_16 == 1 ? 2 : 4), phr->m_bDWhiten, phr->m_bLen);

               DiagPrintf("\nFCS-");
               if (!phr->m_bFCS_16)
                   printHexData( g_stRxMsg.m_ucData + g_stRxMsg.m_unRxDataLength, 4 );
               else
                   printHexData( g_stRxMsg.m_ucData + g_stRxMsg.m_unRxDataLength, 2 );
          }
          #endif

          L4G_StartRx( s_stSelectedTestMode, 0, 0 ); // restart RX
      }
      else if((ucCrtEvent & L4E_EVENT_RX_MSG) && pstRxMsg )
      {
          #ifdef WISUN_FAN_SUPPORT
          if(rfTestVerbose == VB_RAW_PKT)
          {
              //Data
               printHexData( g_stRxMsg.m_ucData, g_stRxMsg.m_unRxDataLength);
               //PHY header
               #ifdef WISUN_FAN_SUPPORT_DEBUG
               DiagPrintf("RX PHY header on RF 0x%04X\n", Lget16(&g_PhrData[0]));
               #endif
               DiagPrintf("RX PHY header 0x%04X\n", Lget16(&g_stRxMsg.m_ucData[0]));
               L4G_STD_PHR* phr = (L4G_STD_PHR*)g_stRxMsg.m_ucData;
               DiagPrintf("RX PHR.MS Flag:0x%X; PHR.FCS:%d bytes, PHR.Whitening:0x%X, PHR.Len:%d bytes\r", phr->m_bMSFlag, (phr->m_bFCS_16 == 1 ? 2 : 4), phr->m_bDWhiten, phr->m_bLen);

               DiagPrintf("\nFCS-");
               if (!phr->m_bFCS_16)
                   printHexData( g_stRxMsg.m_ucData + g_stRxMsg.m_unRxDataLength, 4 );
               else
                   printHexData( g_stRxMsg.m_ucData + g_stRxMsg.m_unRxDataLength, 2 );
          }
          #endif
          rfTestRxPackets++;
          if( rfTestVerbose != VB_OFF )
          {
#ifndef MAC_PHY_SNIFF_SUPPORT
              uint32_t ulPER = HRFPortCrcError*100*100 / (rfTestRxPackets+HRFPortCrcError);
#endif // MAC_PHY_SNIFF_SUPPORT

              if( !memcmp(pstRxMsg->m_ucData+2, c_aMHRPlain, 2 ) )
              {
#ifndef MAC_PHY_SNIFF_SUPPORT
                DiagPrintf ("Rx: %5u  CRC Errors: %5u  Length: %u  Error: %01u.%03u%% RSSI: -%d dBm"  //Warning! RSSI is parsed by Mfg. Software in this message
                  , pstRxMsg->m_ucData[2+2]
                  , HRFPortCrcError
                  , pstRxMsg->m_unRxDataLength - 2
                  , ulPER / 100
                  , ulPER % 100
                  , pstRxMsg->m_ucRSSI
                );
#endif // MAC_PHY_SNIFF_SUPPORT
              }
              else
              {
#ifndef MAC_PHY_SNIFF_SUPPORT
                DiagPrintf( "RX packet no %3d, Length: %4u, RSSI:-%3d dBm, PER: %d.%02d, crc err %3d"
                      , rfTestRxPackets
                      , pstRxMsg->m_unRxDataLength - 2
                      , pstRxMsg->m_ucRSSI
                      , ulPER / 100
                      , ulPER % 100
                      , HRFPortCrcError );
#else
                sniffParseMacPhy( pstRxMsg, targetPANID, targetRouteBPANID, frameDisplayFilter, routeDisplayFilter );
#endif // MAC_PHY_SNIFF_SUPPORT
              }
#ifndef MAC_PHY_SNIFF_SUPPORT
              if( pstRxMsg->m_ucModeSwitch & SWITCH_MODE_EXISTS )
              {
                  DiagPrintf( ", +%d kbps", RegCode_GetDataRateKbps( pstRxMsg->m_ucModeSwitch & L4G_MODE_IDX_MASK ) );
              }
#ifdef REGION_JAPAN  // Prevent collision with Mfg. software on Non-Region-Japan builds
              if( L4G_GET_WHITENING_FLAG(pstRxMsg) )
              {
                  DiagPrintf( ",DW" );
              }
              if( L4G_GET_CRC16_FLAG(pstRxMsg) )
              {
                  DiagPrintf( ",CRC16" );
              }
#endif
                 /* Fstep = 30 MHz Osc / 2^19 bit sigma-delta modulator = 57.22 or 5722 / 100 */
              DiagPrintf( ", FEI %d Hz, LNA %d\n", (signed int)((signed short)g_unLastFEI) * 5722 / 100, (int8_t)g_stRxMsg.m_ucLNA );  //SX1233 LNA is 0 to 6 range step.  CC1200 LNA is +63 to -64 in dB.

           #ifdef DEBUG_DIAG_TEST
              if (rfTestVerbose == VB_RF)
                 CC1200PrintStatusRegisters();  //Unique to CC1200 for ANZ.  Is this still needed???
           #endif

              if( rfTestVerbose == VB_PACKET )
              {
                  printHexData( pstRxMsg->m_ucData, pstRxMsg->m_unRxDataLength );
              }
#endif // MAC_PHY_SNIFF_SUPPORT
          }

          #ifdef WISUN_FAN_SUPPORT
          DiagPrintf("Total Rx packet count:%d\n", rfTestRxPackets);
          DiagPrintf("---------------------------\n");
          #endif

         if (!p_ulExpectedPkHdr ) // is continous RX
         {     // is on echo mode and RX a a diag test packet
               if(  (rfTestDataAck == 'A')

                  && (pstRxMsg->m_unRxDataLength > 2))
               {
                  // send messge on echo
                   if( s_unAckMode && (s_stSelectedTestMode.stOpMode.bStartMode != s_stSelectedTestMode.stOpMode.bNextMode) )
                   {
                       CurrentRfMode_t stAckIdx;

                       stAckIdx.stOpMode.bStartMode = pstRxMsg->m_ucModeSwitch & L4G_MODE_IDX_MASK;
                       stAckIdx.stOpMode.bNextMode = pstRxMsg->m_ucModeSwitch & L4G_MODE_IDX_MASK;
                       stAckIdx.ucAllFlags = 0;

                       L4G_SendPacket( stAckIdx, HalTimerGetValueUs(TIMER_L4G) + TX_DELAY_PERIOD, pstRxMsg->m_ucData+2, pstRxMsg->m_unRxDataLength-2 );
                   }
                   else
                   {
                       L4G_SendPacket( s_stSelectedTestMode, HalTimerGetValueUs(TIMER_L4G) + TX_DELAY_PERIOD, pstRxMsg->m_ucData+2, pstRxMsg->m_unRxDataLength-2 );
                   }

                   if( !(L4E_EVENT_TX_COMPLETE & OS_WaitEventTimed( L4E_EVENT_TX_COMPLETE, 425 )) )
                   {
                      DiagPrintf("\tTX ERROR timeout\n" );
                   }
                   else if( g_ucTxCallbackCode != SFC_SUCCESS )
                   {
                      DiagPrintf("\tTX ERROR %d\n", g_ucTxCallbackCode );
                   }
                   else if( rfTestVerbose == VB_PACKET )
                   {
                      DiagPrintf("\tTX echo back\n" );
                   }
               }
          }
          else if( p_ulExpectedPkHdr == Lget32(pstRxMsg->m_ucData+2) ) // receive the ACK
          {
              if( rfTestVerbose != VB_OFF )
              {
                 DiagPrintf("\tRX echo back\n" );
              }
              break;
          }
          else      // wait for ack but receive something wrong
          {
               if( rfTestVerbose != VB_OFF )
               {
                  DiagPrintf("\tRX ERROR unexpected packet\n" );
               }
               rfTestRxPackets--;
               break;
          }

          L4G_StartRx( s_stSelectedTestMode, 0, 0 ); // restart RX
          usRxCounter = 0;
      }
      else  //Timeout
      {
          if( (++usRxCounter) >= 9 ) // 1000 ms
          {
              L4G_CheckNoise( s_stSelectedTestMode );
              usRxCounter = 0;
          }
          L4G_StartRx( s_stSelectedTestMode, 0, 0 ); // restart RX
      }

      if( !p_ulExpectedPkHdr )
      {
          if( diagRFRuntimeCommands ('R') == ESCAPE_CHAR )  // wait until escape char
          {
             #ifndef COM_ADAPTER
              g_bcheckMACHDR = TRUE;
             #endif // COM_ADAPTER
              break;
          }
      }
      else
      {
          if( (SysTimeGet() - start_time) >= 1000 ) // wait for 1000 ms
          {
              break;
          }
      }

      if (!BUTTON_STATE)
      {
          if (LastButtonEdge == 0)
          {
            break;
          }
      }
      else
      {
          LastButtonEdge = 0;
      }

   }
   L4G_AbortCrtAction();
} // End of diagReceiveTest

/*
** diagRFSendTest -- Command T
**
** Send a batch of test packets over the RF
** The response flag signals if we are the true transmitter, or we
** are reponding to a received data packet, so we should just transmit
** a packet and quit. Response is TRUE when we are sending a response.
*/
void
diagSendTest (unsigned char response)
{
    int i, j;
    char LastButtonEdge = 1;
   #if defined(REGION_JAPAN)
    bool listenBeforeTxFail = FALSE;
    unsigned int rssiValue;
    int failFreq=0;  // Listen before talk - freq where test fails.
    int freq;
    CurrentRfMode_t mode;
   #endif

    if( rfTestPacketLength > L4G_MAX_PK_SIZE )
    {
        rfTestPacketLength = L4G_MAX_PK_SIZE;
    }

    // Make it look like a test packet
    memcpy(s_aTmpDiagBuffer, c_aMHRPlain, sizeof(c_aMHRPlain) );
    memcpy( s_aTmpDiagBuffer+sizeof(c_aMHRPlain), g_oEUI64, MAC_ADDR_LEN );
    i = sizeof(c_aMHRPlain) + MAC_ADDR_LEN;

    if (rfTestDataType == 'C')
    {
        for (; i < rfTestPacketLength; i++)
            s_aTmpDiagBuffer[i] = rfTestData;
    }
    else if(rfTestDataType == 'S')
    {
        for(j = 0; i < rfTestPacketLength; i++)
        {
            s_aTmpDiagBuffer[i] = rfTestString[j++];
            if(j > rfTestStrlen)
            j = 0;
        }
    }
    else if(rfTestDataType == 'I')
    {
        i = 0;
        for( uint8_t uci = 0; i <= ( rfTestPacketLength ); i++, uci++ )
        {
            s_aTmpDiagBuffer[i] = uci;
        }
    }
#ifdef WISUN_PHY_CONF_TEST
    else if(rfTestDataType == 'P')
    {
        DiagPrintf( "\n use PN9 sequence" );;
        rfTestPacketLength = sizeof(pn9_511byte);
        for( i = 0; i < rfTestPacketLength; i++ )
            s_aTmpDiagBuffer[i] = pn9_511byte[i];
    }
   else if(rfTestDataType == 'V')
   {
     DiagPrintf( "\n use 8.3 test vector" );
     rfTestPacketLength = sizeof( WiSUN_test_vector );
     for( i = 0; i < rfTestPacketLength; i++ )
         s_aTmpDiagBuffer[i] = WiSUN_test_vector[i];
   }
#endif // WISUN_PHY_CONF_TEST
    else
    {
        for(; i < rfTestPacketLength; i++)
            s_aTmpDiagBuffer [i] = rand ();
    }

    if ((rfTestDataAck == 'A') && !response)
    {
        Pend (0, 20);           // Give other guy time to get ready

        HRFPortCrcError = 0;
        rfTestRxPackets = 0;
    }
    g_ucTxCallbackCode = 0;  //Clear stale data from B, Q & L Commands.
    rfTestTxSent = 0;
    while ((++rfTestTxSent <= rfTestPacketNumber) || !rfTestPacketNumber)
    {
        #ifndef WISUN_PHY_CONF_TEST
        if( rfTestDataType != 'I' && rfTestDataType != 'P' && rfTestDataType != 'V' )
        {
        s_aTmpDiagBuffer[2] = rfTestTxSent;
        }
        #endif // WISUN_PHY_CONF_TEST
        if( rfTestVerbose != VB_OFF )
        {
            DiagPrintf("TX pk %d, size %d, Pwr %d  Reg %d\n", rfTestTxSent, rfTestPacketLength ,RamMaxPower_M,L4G_RF_GetPAPower());
        }
        if (rfTestVerbose == VB_PACKET)
        {
            printHexData( s_aTmpDiagBuffer, rfTestPacketLength );
        }
        #ifdef WISUN_FAN_SUPPORT
        if (rfTestVerbose == VB_RAW_PKT)
        {
            printHexData( s_aTmpDiagBuffer, rfTestPacketLength );
        }
        #endif

        //Special option for testing MAC layer encryption
        //if( rfTestDataType == 'E' )
        //{
        //  uint8_t result;
        //  result = L4E_diagSendFrame(rfTestPacketLength, g_unLastChannel);
        //  DiagPrintf("Diag L4E Frame: %d\n", result);
        //  return;
        //}

       #if defined(REGION_JAPAN)
        // Listen before Transmit Mode. Check RSSI value before being allowed to transmit in this mode.
        // We need to test RSSI at freq and also at freq-100 KHz and freq+100 KHz.
        if (listenBeforeTx)
        {
            mode.stOpMode = s_stSelectedTestMode.stOpMode;
            mode.ucAllFlags = 0;
            freq = RegCode_GetFrequencyKHz( g_unLastChannel );
            L4G_StartRx( mode, 0, 0 ); // Start RX
            Pend (0,10);

            DiagPrintf("Listen before Talk. Using channel=%d (%d.%d MHz).\n",g_unLastChannel,freq/1000,freq%1000 );

            for (int i=0;i<5;i++)
            {
               for (int j=freq-100;j<=freq+100;j+=100)
               {
                   RF_setRfFreqkHz(j);
                   CC1200StartRX();
                   Pend (0,10);
                   rssiValue = L4G_RF_ReadRSSI();

                   if( rssiValue < 83 )
                   {
                      listenBeforeTxFail = TRUE;
                      failFreq = j;
                      break;
                   }
               }
               // No need to continue testing other frequencies if we fail.
               if (listenBeforeTxFail) break;
            }
            // Restore freq to what it was before the Listen before Talk test.
            RF_setRfFreqkHz(freq);
        }

        if (listenBeforeTxFail)
        {
            RF_setRfFreqkHz(freq);
            DiagPrintf("TX fail: RSSI too high. Not safe to TX. RSSI=%d. Failed at freq=%d.%d MHz.\n",rssiValue,failFreq/1000,failFreq%1000);
#if defined(GRIDSTREAM_SBS) && defined(REGION_JAPAN) && defined(S5_EV_LCE)
   #warning "return statment removed for ARIB test in V11.02.009"
#else
            return;
#endif
        }
       #endif // REGION_JAPAN


        L4G_SendPacket(
                     #ifdef WISUN_PHY_CONF_TEST
                      L4G_GetCurrentRfMode( s_stSelectedTestMode.stOpMode.bStartMode,
                                            s_stSelectedTestMode.stOpMode.bNextMode,
                                            s_stSelectedTestMode.ucAllFlags,
                                            L4G_CRC16_MASK,
                                            0,
                                            RamMaxPower_M),
                     #else
                      s_stSelectedTestMode,
                     #endif // WISUN_PHY_CONF_TEST
                      HalTimerGetValueUs(TIMER_L4G) + 3000,
                      s_aTmpDiagBuffer,
                      rfTestPacketLength );

        if( !(L4E_EVENT_TX_COMPLETE & OS_WaitEventTimed( L4E_EVENT_TX_COMPLETE, 425 )) )
        {
            DiagPrintf("TX ERROR timeout\n" );
        }
        else if( g_ucTxCallbackCode != SFC_SUCCESS )
        {
            DiagPrintf("TX ERROR %d\n", g_ucTxCallbackCode );
        }
        else if (rfTestDataAck == 'A')
                diagReceiveTest (*(uint32_t*)s_aTmpDiagBuffer);


        #ifdef WISUN_FAN_SUPPORT
        if (rfTestVerbose == VB_RAW_PKT)
        {
                //PHY header
                #ifdef WISUN_FAN_SUPPORT_DEBUG
                DiagPrintf("Tx PHY Header: 0x%04X\n", Lget16(&g_PhrData[0]));
                #endif
                uint8_t  phr[2] ;
                memcpy(phr, &g_stTxParams.m_sPhyHdr, 2);
                DiagPrintf("Tx PHY Header on RF: 0x%04X\n", Lget16(&phr[0]));
                //extract the length from PHY header
                uint16_t len  = g_aucMsbTranslate[ phr[0] & 0xE0 ];
                len  = (len << 8) | g_aucMsbTranslate[ phr[1] ];
                #ifdef WISUN_FAN_SUPPORT_DEBUG
                DiagPrintf("Tx CC1200_PREAMBLE_CFG1: 0x%X,CC1200_SYNC0: 0x%X,CC1200_SYNC1: 0x%X\n",CC1200ReadRegister (CC1200_PREAMBLE_CFG1),CC1200ReadRegister (CC1200_SYNC0),CC1200ReadRegister (CC1200_SYNC1));
                #endif
                DiagPrintf("TX PHR.MS Flag:0x%X; PHR.FCS:%d bytes, PHR.Whitening:0x%X, PHR.Len:%d bytes\n\r", g_stTxParams.m_sPhyHdr.m_bMSFlag, ((g_stTxParams.m_sPhyHdr.m_bFCS_16 == 1) ? 2 : 4), g_stTxParams.m_sPhyHdr.m_bDWhiten, len);
                // FCS
                #ifdef WISUN_FAN_SUPPORT_DEBUG
                if (g_stTxParams.m_sPhyHdr.m_bDWhiten)
                {
                    DiagPrintf("\nFCS before Whitening-");
                    uint8_t crc[4];
                    if( !g_stTxParams.m_sPhyHdr.m_bFCS_16 )
                    {
                        memcpy( crc, &g_L4G_CRC, L4G_CRC32_SIZE );
                        printHexData( crc, L4G_CRC32_SIZE );
                    }
                    else
                    {
                        memcpy( crc, &g_L4G_CRC, L4G_CRC16_SIZE );
                        printHexData( crc, L4G_CRC16_SIZE );
                    }
                }
                #endif /* WISUN_FAN_SUPPORT_DEBUG */
                DiagPrintf("FCS-");
                if( !g_stTxParams.m_sPhyHdr.m_bFCS_16 )
                    printHexData( g_stTxParams.m_aData + rfTestPacketLength, 4 );
                else
                    printHexData( g_stTxParams.m_aData + rfTestPacketLength, 2 );
                DiagPrintf("---------------------------\n");
            }
        #endif /* WISUN_FAN_SUPPORT */

        if (diagRFRuntimeCommands ('T') == ESCAPE_CHAR)
        {
            ++rfTestTxSent;       // Fake out the printout
            break;
        }

        if (!BUTTON_STATE)
        {
            if (LastButtonEdge == 0)
            {
                ++rfTestTxSent;       // Fake out the printout
                break;
            }
        }
        else
        {
            LastButtonEdge = 0;
        }
        Pend (0, rfTestTxDelay);
    }

    DiagPrintf("Packets Sent %u, Packets Rx %u\n", --rfTestTxSent,
         (rfTestDataAck == 'A') ? rfTestRxPackets : 0L);


} /* End of diagSendTest */


/*
** diagRSSITest -- Command E
**
** Simple test that either takes a single sample of RSSI or spins in a loop and continues to
** measure RSSI over an extended period where it then reports the MIN/AVE/MAX.
*/
void
diagRSSITest (void)
{
  unsigned char response;
  uint32_t stop_time;
  uint32_t readCount;
  uint8_t  adcReadinDbm, maxRSSI, minRSSI;
  uint32_t adcAccumReads;
  uint32_t avgRSSI=0;

  // Use 'B' command with transmitter (continuous transmit) to test.

  response = 0xdf & DCDiagGetChar ("(S)ingle Read or (M)ulti Read with metrics: ", "SsMm");

  L4G_StartRx( s_stSelectedTestMode, 0, 0 ); // start RX forr rssi measurement
  Pend (0,1);

  if(response == 'M')
  {
    /* This will do a read every 10ms */
    stop_time = DCDiagGetInt32 ("How many seconds (1-60)?: ", 1, 60, 10);
    stop_time = SysTimeGet() + (stop_time * 1000);

    adcAccumReads = 0;
    readCount = 0;
    maxRSSI = 0;
    minRSSI = 127;
    while (SysTimeGet() < stop_time)
    {
      adcReadinDbm  = L4G_CheckNoise(s_stSelectedTestMode);
      Pend(0,10);

      if (adcReadinDbm != 128)
      {
        DiagPrintf("RSSI = -%3d dBm\n", adcReadinDbm);

        //Search for max
        if(adcReadinDbm > maxRSSI)
          maxRSSI = adcReadinDbm;

        //Search for min
        if(adcReadinDbm < minRSSI)
          minRSSI = adcReadinDbm;

        //Accumulate for average RSSI
        readCount++;
        adcAccumReads += adcReadinDbm;
      }
      else
      {
        DiagPrintf("RSSI = -%d (invalid)\n", adcReadinDbm);
      }
    }

    if (readCount == 0)
    {
      DiagPrintf("No signal...\n");
    }
    else
    {
      //Calculate the average
      avgRSSI = adcAccumReads/readCount;

      DiagPrintf("Average RSSI: -%3d dBm\n", avgRSSI );
      DiagPrintf("Min RSSI    : -%3d dBm\n", maxRSSI );
      DiagPrintf("Max RSSI    : -%3d dBm\n", minRSSI );
    }
  }
  else
  {
    adcReadinDbm = L4G_CheckNoise(s_stSelectedTestMode);
    DiagPrintf("RSSI: -%d dBm\n", adcReadinDbm);
  }
  L4G_StartRx( s_stSelectedTestMode, 0, 0 ); // Re-start RX
  Pend (0,1);
}



/*
** diagLockTestChannel
**
** Go to channel, record how long it take and return. Another function most likely
** will call this over and over.
*/
void
diagLockTestChannel( uint32_t p_unChannel, uint32_t p_unMode )
{
   unsigned int stop_time, i;

      /*
      ** We used to time from "after" the call the Set Channel until the PLL is locked. But
      ** with some of the new RF chips like the CC1200 for example, we will be locked by the
      ** time we return from Set Channel. So from now on, start measuring the entire time to
      ** set the channel and to get locked.
      */
    microTimerStart();

    L4G_RF_SetChannel (p_unChannel);

     if( p_unMode == 'R' )
     {
      /* PLL Lock not done in Rx Settings.  Use separate call. */
      L4G_RF_SetRXRFSettings( s_stSelectedTestMode );
         if( ! L4G_RF_waitPllLock() )
         {
             DiagPrintf("error on pll lock\n");
             rfTestLockFails++;
             return;
         }
     }
     else
     {
         /* CC1200 & SX1233 do PLL Lock in this call.  Si4467 & EFR32 PLL delay is hidden in API.  */
         if( !L4G_RF_SetTXRFSettings( s_stSelectedTestMode ) )
         {
             DiagPrintf("error on pll lock\n");
             rfTestLockFails++;
             return;
         }
     }
    stop_time = microTimerStop();

    rfTestLockSamples++;
    rfTestLockTotalTime += stop_time;

    if (stop_time > rfTestLockMax)
        rfTestLockMax = stop_time;

    if (stop_time >= 10000)
      rfTestLockFails++;

     if (p_unMode == 'T')
      {
      // Make it look like a test packet
      memcpy(s_aTmpDiagBuffer, c_aMHRPlain, sizeof(c_aMHRPlain) );
      memcpy( s_aTmpDiagBuffer+sizeof(c_aMHRPlain), g_oEUI64, MAC_ADDR_LEN );
      i = sizeof(c_aMHRPlain) + MAC_ADDR_LEN;

      if( rfTestCharacter == 0xFE )
      {
      for(; i < rfTestPacketLength; i++)
         s_aTmpDiagBuffer [i] = rand ();
      }
      else
      {
      for(; i < rfTestPacketLength; i++)
         s_aTmpDiagBuffer[i] = rfTestCharacter;
      }

      L4G_SendPacket( s_stSelectedTestMode, HalTimerGetValueUs(TIMER_L4G) + TX_DELAY_PERIOD, s_aTmpDiagBuffer, rfTestPacketLength );

      Pend (0, rfTestLockDelay);
      L4G_AbortCrtAction();
      }
    else
      Pend (0, rfTestLockDelay);
}


/*
** diagLockTest -- Command L
**
** We need a couple of things from ticker.c so we can use the normal hopping sequence. The ticker isn't needed
** when we use diagnostic mode, so just call the basic setup routine to build the hopping sequence.
*/
void
diagLockTest (void)
{
   unsigned int  channel_1, channel_2;
   unsigned char mode_1, mode_2, mode_3;
   uint32_t      unChNo, rfTestLockPriorChannel;

   rfTestLockMax = 0;
   rfTestLockFails = 0;
   rfTestLockSamples = 0;
   rfTestLockTotalTime = 0;
   rfTestLockPriorChannel = g_unLastChannel;  //This test wrecks Last Channel, so save it.

    // init Channel Hopping sequence to Discovery Channel Bitmap
   #if defined(WISUN_FAN_SUPPORT)
    CSMA_SetChannelList();
   #else
    L4E_SetExtendedHoppingSequence( 0, RegionGetChannelMask(), 0 );
   #endif //WISUN_FAN_SUPPORT

   // __toupper( get_char () )
   mode_3 = 0xdf & DCDiagGetChar("Do 'Manufacturing walk', 'Walking' or 'Ping pong' test [M|W|P]? ", "MmWwPp");

   if(mode_3 != 'W')
   {
       // Sort the Valid Channel List. Recycle variables
       do {
           mode_1 = 0;
           for (mode_2 = 0; mode_2 < g_stHopSeq.m_unSequenceLen-1; mode_2++)
               if (g_stHopSeq.m_aunSequence[mode_2] > g_stHopSeq.m_aunSequence[mode_2+1])
               {
                   channel_1 = g_stHopSeq.m_aunSequence[mode_2];
                   g_stHopSeq.m_aunSequence[mode_2] = g_stHopSeq.m_aunSequence[mode_2+1];
                   g_stHopSeq.m_aunSequence[mode_2+1] = channel_1;
                   mode_1 = 1;
               }
          } while (mode_1);

   }
   if(mode_3 != 'M')
   {
      DiagPrintf ("Enter character to output (hex, FE for random): ");
      rfTestCharacter = (unsigned char) DCDiagGetHexString (2, rfTestData);
   }

   if(mode_3 == 'P')
   {
      DiagPrintf("Valid Channel List:");
      for (rfTestLockChannel=0; rfTestLockChannel < g_stHopSeq.m_unSequenceLen; rfTestLockChannel++)
      {
          if (!(rfTestLockChannel & 0x1F)) DiagPrintf("\n");
          DiagPrintf("%d ",g_stHopSeq.m_aunSequence[rfTestLockChannel]);
      }
      DiagPrintf("\n");
      unChNo = RegCode_GetMaxChannels();
      if(!( rf_checkChannelNumber( channel_1 = (unsigned int) DCDiagGetInt32("Enter first channel [From List]: ", 0, unChNo, g_stHopSeq.m_aunSequence[0])) )) return;
      mode_1 = 0xdf & DCDiagGetChar("Transmit or Receive [T|R]? ", "TRtr");

      if(!( rf_checkChannelNumber( channel_2 = (unsigned int) DCDiagGetInt32("Enter second channel [From List]: ", 0, unChNo, g_stHopSeq.m_aunSequence[1])) )) return;
      mode_2 = 0xdf & DCDiagGetChar("Transmit or Receive [T|R]? ", "TRtr");
   }
   else
   {
      if(mode_3 == 'W')
      {
         mode_1 = 0xdf & DCDiagGetChar("Transmit only (T) or Both (B) [T|B]? ", "TtBb");
      }
      else
      {
         mode_1 = 'B';
         rfTestLockDelay = 2;
      }
   }

   if(mode_3 != 'M')
      rfTestLockDelay = (unsigned int) DCDiagGetInt32 ("Enter delay in milliseconds between channels [2 - 30000]: ",
                                     2, 30000, rfTestLockDelay);

   if (mode_3 == 'P')
   {
      DiagPrintf("<Hit Esc to exit test>\n");
         /* Keep number of samples reigned in */
        while (rfTestLockSamples < 65500)
            {
            if(!(rfTestLockSamples & 0xf))
                DiagOut ('.');

            diagLockTestChannel( channel_1, mode_1 );

            if (diagRFRuntimeCommands ('l') == ESCAPE_CHAR)
                break;

            diagLockTestChannel( channel_2, mode_2 );

            if (diagRFRuntimeCommands ('l') == ESCAPE_CHAR)
                break;
            if(!(rfTestLockSamples & 0x3FF))
                DiagOut ('\n');
            }
   }
   else
   {
      DiagPrintf("\nChannel Count:%d  Channel List:",g_stHopSeq.m_unSequenceLen);
      for (rfTestLockChannel=0; rfTestLockChannel < g_stHopSeq.m_unSequenceLen; rfTestLockChannel++)
      {
          if (!(rfTestLockChannel & 0x1F)) DiagPrintf("\n");
         DiagPrintf("%d ",g_stHopSeq.m_aunSequence[rfTestLockChannel]);
      }
      DiagPrintf("\n<Hit Esc to exit test>\n");
      rfTestLockChannel = 0;

       /* Keep number of samples reigned in */
     while (rfTestLockSamples < 65500)
      {
         if(!(rfTestLockChannel & 0x0F))
            DiagOut ('.');

         diagLockTestChannel( g_stHopSeq.m_aunSequence[rfTestLockChannel], 'T' );

         if (diagRFRuntimeCommands ('L') == ESCAPE_CHAR)
            break;

         if(mode_1 == 'B')
         {
            diagLockTestChannel( g_stHopSeq.m_aunSequence[rfTestLockChannel], 'R' );

            if(diagRFRuntimeCommands ('L') == ESCAPE_CHAR)
               break;
         }

         rfTestLockChannel++;
         if (rfTestLockChannel == g_stHopSeq.m_unSequenceLen)
            rfTestLockChannel = 0;

         if(!(rfTestLockSamples & 0x3FF))
            DiagOut ('\n');
      }
   }
   g_unLastChannel = rfTestLockPriorChannel;  //Restore last set channel

      // Quit if no samples
   if(!rfTestLockSamples)
      return;

      // Use as a work variable
   channel_1 = (unsigned int) ((rfTestLockTotalTime + (rfTestLockSamples >> 1))
                    / rfTestLockSamples);
   channel_2 = (rfTestLockFails * 100 + (rfTestLockSamples >> 1))
            / rfTestLockSamples;

   DiagPrintf ("\nLock Summary:\n"
         "    Max Lock Time:  %2d uSec\n"
         "    Ave Lock Time:  %2d uSec\n"
         "     Lock Success:  %d / %d (%d%%)\n",
         rfTestLockMax, channel_1,
         rfTestLockSamples - rfTestLockFails,
         rfTestLockSamples, 100 - channel_2);
}


/*
** diagRfTestMode()  Common code instance for all radio chip types.
**
*/
void
diagRFTestMode (unsigned int c)
{
   unsigned int i, reg;

#if defined(S5_EV_LCE) || defined(S5_TOSHIBA_AP)
   uint16_t       latchRFPower[4];
   unsigned char  latchPowerLevel;
   unsigned char  latchTempComp;
   memcpy (latchRFPower, HFCData.RFPower, sizeof(latchRFPower));
   latchPowerLevel = RamMaxPower_M;
   latchTempComp = TRUE;
#endif

   memcpyReverse( g_oEUI64, g_stPersistMnf.m_unMAC, MAC_ADDR_LEN );
   rfTestDataAck = 'N';
   g_stLastRfModeIdx.stOpMode.bStartMode = 0x0F;  //Force update of RF Parameters at L4G_RF level.
   g_stLastRfModeIdx.stOpMode.bNextMode = 0x0F;
   g_stLastRfModeIdx.ucAllFlags = 0xFF;

   while(1)
      {
      switch(c)
         {
         case '1':
            SixConfigByte_M ^= ANTENNA_SELECT;
            DiagPrintf("RF Antenna: %s\n", RamRFAntenna_M ? "Ext" : "Int");
            HRFSetAntenna();
            break;

         case '2':
            L4G_RF_PrintRadioRegisters();
            break;

            /* NB: Commands 3 has been moved to the Hardware-Specific diag.c files. */

#if defined(S5_EV_LCE) || defined(S5_TOSHIBA_AP)
         case '4':
            if ((mh_only == MH_EVLCE_INDIA) || (mh_only == MH_GENERAL_U_ANZ) || (mh_only == MH_EVLCE_BRAZIL_ABNT)
                || (mh_only == MH_GENERAL_U_LGAX) || (mh_only == MH_GENERAL_S1100_AP))
            {
               if (!latchTempComp)
               {
                  HFCData.RFPower[0] |= (latchRFPower[0] & 0xFF00);
                  HFCData.RFPower[1] |= (latchRFPower[1] & 0xFF00);
                  HFCData.RFPower[2] |= (latchRFPower[2] & 0xFF00);
               #ifdef RF_ONE_WATT
                  HFCData.RFPower[3] |= (latchRFPower[3] & 0xFF00);
               #endif
               }
               else
               {
                     /* Strip off dBm values which turns off temperature compensation */
                  HFCData.RFPower[0] &= 0xFF;
                  HFCData.RFPower[1] &= 0xFF;
                  HFCData.RFPower[2] &= 0xFF;
               #ifdef RF_ONE_WATT
                  HFCData.RFPower[3] &= 0xFF;
               #endif
               }
               latchTempComp = !latchTempComp;
               DiagPrintf("RF Temperature compensation %s\n", latchTempComp ? "enabled" : "disabled");
            }
            else
            {
               DiagPrintf("RF Temperature compensation NOT SUPPORTED\n");
            }
            break;
#endif  //S5_EV_LCE

         case '5':
            rf_config4e( FALSE );
            break;

            /* NB: Commands 7 and 8 have been moved to the Hardware-Specific diag.c files. */

         case 'B':
            diagTransmitterTest();
            break;

         case 'D':
            rfTestDataType = 'C';
            rfTestDataAck = 'N';
            rfTestPacketLength = 512;
            rfTestPacketNumber = 100;

            HRFPortCrcError = 0;
            rfTestData = 0x55;
            rfTestTxDelay = 300;
            rfTestLockDelay = 20;
            rfTestVerbose = VB_ON;
            rfTestCharacter = 0x55;

            /* Make sure board is initialized and channel set */
            g_ucAssociationStatus = L4E_DEVICE_ASSOCIATED;
            L4G_RF_Init();
            L4G_AbortCrtAction();

            //init Channel Hopping sequence to Discovery Channel Bitmap
           #if defined(WISUN_FAN_SUPPORT)
            CSMA_SetChannelList();
           #else
            L4E_SetExtendedHoppingSequence( 0, RegionGetChannelMask(), 0 );
           #endif //WISUN_FAN_SUPPORT
            L4G_RF_SetChannel(  g_stHopSeq.m_aunSequence[0] );
            s_stSelectedTestMode.stOpMode.bStartMode = L4E_DEF_MODE_IDX;
            s_stSelectedTestMode.stOpMode.bNextMode = L4E_DEF_MODE_IDX;
            s_stSelectedTestMode.ucAllFlags = 0;
            s_unAckMode = 0;
            rftest_check4eHeader = 1;
            break;

         case 'E':
            diagRSSITest();
            break;

         case 'F':
            {  //The change to Mode 1,2,3,4 was considered a "critical change" made March 2016 that forced Mfg. software to adapt.
               //Warning: Mfg. software assumes a fixed number of <CR> to cycle through this menu.  New items impact production.
#if !defined(WISUN_FAN_SUPPORT)
  #ifdef REGION_JAPAN
              const unsigned int c_aBauds[] = { 50, 100, 200 };
              CurrentRfMode_t stCrtMode = s_stSelectedTestMode;
              CurrentRfMode_t oldStCrtMode = s_stSelectedTestMode;
              switch( DCDiagGetInt32("Enter RF baud rate [50, 100, 200] kbps: ", 50, 200, c_aBauds[s_stSelectedTestMode.stOpMode.bStartMode])  )
              {
                case 50:
                  stCrtMode.stOpMode.bStartMode = 0;
                  break;
                case 100:
                  stCrtMode.stOpMode.bStartMode = 1;
                  break;
                case 200:
                  stCrtMode.stOpMode.bStartMode = 2;
                  break;
                default:
                  stCrtMode.stOpMode.bStartMode = 0;
                  DiagPrintf( "invalid 4G baudrate\r\n");
              }

              switch( DCDiagGetInt32("Enter switch mode baud rate [50, 100, 200] kbps: ", 50, 300, c_aBauds[s_stSelectedTestMode.stOpMode.bNextMode] )  )
              {
                case 100:
                  stCrtMode.stOpMode.bNextMode = 1;
                  break;
                case 200:
                  stCrtMode.stOpMode.bNextMode = 2;
                  break;
              }
              // Reset the channel to 0 if the baud rate changed. Otherwise a TX ERROR might occur if we were using a channel that
              // is not supported by the new baud rate (ex. Channel 37 at 50kbps).
              if ( (oldStCrtMode.stOpMode.bStartMode != stCrtMode.stOpMode.bStartMode) ||
                   (oldStCrtMode.stOpMode.bNextMode != stCrtMode.stOpMode.bNextMode) )
              {
                L4G_RF_SetChannel(0);
              }
  #else // Region Japan
              CurrentRfMode_t stCrtMode;
              stCrtMode.stOpMode.bStartMode = 0;
              stCrtMode.stOpMode.bNextMode = 0;
              stCrtMode.ucAllFlags = 0;
              s_stSelectedTestMode.stOpMode.bStartMode = DCDiagGetInt32("Enter Start Mode: ", 1, 4, (1+L4E_DEF_MODE_IDX));
              if( !RegionIsValidOperatingMode(s_stSelectedTestMode.stOpMode.bStartMode) )
              {
                  DiagPrintf("Invalid 802.15.4g Operating Mode for the selected country code %d\r\n", RamChanMask_M);
                  s_stSelectedTestMode.stOpMode.bStartMode = 0;
                  break;
              }
              switch(s_stSelectedTestMode.stOpMode.bStartMode)
              {
                  case 1:  stCrtMode.stOpMode.bStartMode = 0; break;
                  case 2:  stCrtMode.stOpMode.bStartMode = 1; break;
                  case 3:  stCrtMode.stOpMode.bStartMode = 2; break;
                  case 4:  stCrtMode.stOpMode.bStartMode = 3; break;
              }
              s_stSelectedTestMode.stOpMode.bStartMode = DCDiagGetInt32("Enter Switch Mode: ", 1, 4, (1+L4E_DEF_MODE_IDX));
              if( !RegionIsValidOperatingMode(s_stSelectedTestMode.stOpMode.bStartMode) )
              {
                  DiagPrintf("Invalid 802.15.4g Operating Mode for the selected country code %d\r\n", RamChanMask_M);
                  s_stSelectedTestMode.stOpMode.bStartMode = 0;
                  break;
              }
              switch( s_stSelectedTestMode.stOpMode.bStartMode )
              {
                  case 1: stCrtMode.stOpMode.bNextMode = 0; break;
                  case 2: stCrtMode.stOpMode.bNextMode = 1; break;
                  case 3: stCrtMode.stOpMode.bNextMode = 2; break;
                  case 4: stCrtMode.stOpMode.bNextMode = 3; break;
                  default: stCrtMode.stOpMode.bNextMode = 0; DiagPrintf("Invalid 802.15.4g Operating Mode\r\n");
              }
#endif // Region Japan

			  // Added spaces after colon in 4 places for Mfg. software parse uniformity per C. Thongsouk Feb 2017.
              if( DCDiagGetInt32("Enter data whitening: ", 0, 1, s_stSelectedTestMode.stFlags.bDataWhiten ? 1 : 0)  )
              {
                  stCrtMode.stFlags.bDataWhiten = 1;
              }

#ifdef REGION_JAPAN  //This message conflicts with Non-Region-Japan Mfg. software
              if( DCDiagGetInt32 ("Enter CRC16: ", 0, 1, s_stSelectedTestMode.stFlags.bCrc16 ? 1 : 0)  )
              {
                  stCrtMode.stFlags.bCrc16 = 1;
              }
#endif
              if( DCDiagGetInt32("Enter CCA: ", 0, 1, s_stSelectedTestMode.stFlags.bCca ? 1 : 0)  )
              {
                  stCrtMode.stFlags.bCca = 1;
              }

              s_stSelectedTestMode = stCrtMode;

  #ifndef REGION_JAPAN  //This query and bit rate display do not appear in Region Japan builds
              s_unAckMode = DCDiagGetInt32("AckMode: ", 0, 1, s_unAckMode);
              rf_displayRfBaudrate();
  #endif

#else //WISUN_FAN_SUPPORT

              if( DCDiagGetInt32("Enter data whitening: ", 0, 1, s_stSelectedTestMode.stFlags.bDataWhiten ? 1 : 0)  )
              {
                  s_stSelectedTestMode.stFlags.bDataWhiten = 1;
              }
              if( DCDiagGetInt32 ("Enter CRC16 (0=CRC32, 1=CRC16):", 0, 1, s_stSelectedTestMode.stFlags.bCrc16 ? 1 : 0)  )
              {
                  s_stSelectedTestMode.stFlags.bCrc16 = 1;
              }
              if( DCDiagGetInt32("Enter CCA: ", 0, 1, s_stSelectedTestMode.stFlags.bCca ? 1 : 0)  )
              {
                  s_stSelectedTestMode.stFlags.bCca = 1;
              }

              do /* for configurable preamble length */
              {
                  g_unPhyFSKPreambleLength =
                     DCDiagGetInt32("Enter Preamble Length 0-Default/1/2/3/4/5/6/7/8/12/24: ",
                                      0, 24, g_unPhyFSKPreambleLength);
              } while( (g_unPhyFSKPreambleLength > 8 && g_unPhyFSKPreambleLength < 12) ||
                       (g_unPhyFSKPreambleLength > 12 && g_unPhyFSKPreambleLength < 24) ||
                       (g_unPhyFSKPreambleLength > 24) );

#endif //WISUN_FAN_SUPPORT
            }
            break;

         case 'G':
            diagRFRuntimeCommands('D'); //Decrement Tx Power. Region Japan outside of commands.
            break;

         case 'H':
            {
               uint32_t unChNo = RegCode_GetMaxChannels();
               if( rf_checkChannelNumber( unChNo = DCDiagGetInt32("Set to channel: ", 0, unChNo, g_stHopSeq.m_aunSequence[0])) )
               {
                  L4G_RF_SetChannel( unChNo );
               }
               else
               {
                  DiagPrintf("Invalid Channel %d\n", unChNo);
               }
               break;
            }

            /* Write RF register */
         case 'J':
            DiagPrintf("Enter RF Register to write: ");
            #ifdef SERIES5
               reg = (unsigned int)DCDiagGetHexString(2, 00);
            #else  //Both CC1200 and Si4467
               reg = (unsigned int)DCDiagGetHexString(4, 00);
            #endif
            DiagPrintf ("Enter value to write: ");
            i = DCDiagGetHexString( 2, L4G_RF_ReadRadioRegister(reg) );
            L4G_RF_WriteRadioRegister( reg, (unsigned int)i );
            L4G_RF_SetTabulatedModeRegValues(s_stSelectedTestMode.stOpMode.bStartMode, reg, i );
            break;

            /* Read RF register */
         case 'K':
            DiagPrintf ("Enter RF Register to read: ");
            reg = (unsigned int) DCDiagGetHexString (4, 00);
            DiagPrintf ("\n Register 0x%02X contains 0x%02X", reg, L4G_RF_ReadRadioRegister(reg) );
            break;

         case 'L':
            diagLockTest();
            break;

         case 'N':
            rfTestPacketNumber = (unsigned int)
               DCDiagGetInt32 ("Enter # of test packets to send (0 is continuous) [0 - 1024]: ",
                                       0, 1024, rfTestPacketNumber);

            #ifdef WISUN_FAN_SUPPORT
            rfTestPacketLength = (unsigned int)
              DCDiagGetInt32 ("Enter Length of test packets [1 - 2047]: ",
                              1, 2047, rfTestPacketLength);
            #else
            rfTestPacketLength = (unsigned int)
              DCDiagGetInt32 ("Enter Length of test packets [1 - 1508]: ",
                              1, 1508, rfTestPacketLength);
            #endif

            rfTestTxDelay = (unsigned int)
              DCDiagGetInt32 ("Enter Tx inter packet delay in milliseconds [10 - 10000]: ",
                             10, 10000, rfTestTxDelay);

           rftest_check4eHeader = (unsigned char)
             DCDiagGetInt32 ("Check 4e header [1/0]: ",
                             0, 1, rftest_check4eHeader);
            break;

         case 'Q':
            rf_square_wave();
            break;

         case 'P':
            rf_ping_to();
            break;

         case 'M':
            rf_receive_ping(RECEIVE_PING);
            break;

         case 'R':
            diagReceiveTest(0);
            break;

         case 'S':
            DiagPrintf ("Packet Header and Data\n"
                  "      Packet Length: %u Bytes\n"
                  "  Number of Packets: %u\n"
                  "          Data Mode: %s\n"
                  "          Data Type: ",
                  rfTestPacketLength,
                  rfTestPacketNumber,
                  (rfTestDataAck == 'N') ? "NoAck" : "Ack");

            if (rfTestDataType == 'C')
               DiagPrintf ("Constant using %02x\n", rfTestData);

            else if (rfTestDataType == 'S')
               {
               DiagPrintf ("String: ");
               for (i = 0; i < rfTestStrlen; i++)
                  DiagPrintf("%02x ", rfTestString[i]);
               DiagPrintf("\n");
               }
           #ifdef WISUN_PHY_CONF_TEST
            else if (rfTestDataType == 'I')
               DiagPrintf("Incremental value without MHR\n");

            else if (rfTestDataType == 'P')
               DiagPrintf("PN9\n");

            else if (rfTestDataType == 'V')
               DiagPrintf("Wi-SUN PHY Conformance 8.3 vector\n");
           #endif // WISUN_PHY_CONF_TEST
            else
               DiagPrintf ("Random\n");

            DiagPrintf ("Others\n"
                  "    Tx Packet Delay: %u milliseconds\n"
                  "         CRC Errors: %u\n",
                  rfTestTxDelay,
                  HRFPortCrcError );

            rf_displayChannel(g_unLastChannel);
            rf_displayRfBaudrate();
            break;

         case 'T':
            diagSendTest(FALSE);
            break;

         case 'U':
            diagRFRuntimeCommands('U'); //Increment Tx Power. Region Japan outside commands.
            break;

         case 'v':
         case 'V':
            diagBumpVerboseMode();
            break;

         case 'W':
            listenBeforeTx = !listenBeforeTx;
            if (listenBeforeTx)
               DiagPrintf("Listen before TX is ON\n");
            else
               DiagPrintf("Listen before TX is OFF\n");
            break;

         case 'X':
         #if defined(S5_EV_LCE) || defined(S5_TOSHIBA_AP)
            /* Upon Exit, Undo Power Setting Changes. */
            if ((mh_only == MH_EVLCE_INDIA) || (mh_only == MH_GENERAL_U_ANZ) || (mh_only == MH_EVLCE_BRAZIL_ABNT)
                || (mh_only == MH_GENERAL_U_LGAX) || (mh_only == MH_GENERAL_S1100_AP))
            {
               memcpy (HFCData.RFPower, latchRFPower, sizeof(latchRFPower));
               RamMaxPower_M = latchPowerLevel;
            }
         #endif
            return;

         case 'Z':
           #ifdef WISUN_PHY_CONF_TEST
            rfTestDataType = 0xdf & DCDiagGetChar ("Random, Constant, or String Data [R|C|S|I|P|V]? ", "RCSIPVrcsipv");
           #else
            rfTestDataType = 0xdf & DCDiagGetChar ("Random, Constant, or String Data [R|C|S|I]? ", "RCSIrcsi");
           #endif //WISUN_PHY_CONF_TEST

            if (rfTestDataType == 'C')
               {
               DiagPrintf ("Enter hex character to use: ");
               rfTestData = (unsigned char) DCDiagGetHexString ( 2, rfTestData);
               }

            else if (rfTestDataType == 'S')
               {
               rfTestStrlen = (unsigned int) DCDiagGetInt32 (
                              "Enter string length [1 - 16]: ", 1, 16, rfTestStrlen);

               for (i = 0; i < rfTestStrlen; i++)
                  {
                  DiagPrintf ("Enter string character #%02u: ", i);
                  rfTestString [i] = (unsigned char) DCDiagGetHexString (2, rfTestString[i]);
                  }
               }

            rfTestDataAck = 0xdf & DCDiagGetChar ("Data NoAck (N) or Data Ack (A) [N|A]? ", "ANan");
            break;

            /* Just ignore an escape */
         case ESCAPE_CHAR:
            break;

         default:
               /*
               ** If not a standard command, call the device specific routine to
               ** see if maybe it is a custom command.
               */
            if (HDiagRfTest (c))
               {
            DiagPrintf ("Choose one of the following commands\n"
                  "  1 - Toggle Antenna Selection\n"
                  "  2 - Display current radio chip registers\n"
               #if defined(S5_EV_LCE)
                  "  4 - Toggle RF Transmit temperature compensation\n"
               #endif
                  "  5 - 4e config\n"
                  "  B - Turn transmitter on\n"
                  "  D - Reset parameters\n"
                  "  E - RSSI Test\n"
                  "  F - Set RF Baud Rate\n"
               #ifdef REGION_JAPAN
				  " G/U- Step power output down/up\n"
               #endif
                  "  H - Set RF Tx/Rx Channel\n"
                  "  J - Write RF register\n"
                  "  K - Read RF register\n"
                  "  L - PLL Lock Test\n"
                  "  M - Rx packet and send ACK\n"
                  "  N - Specify Number, Length, and Timing of test packets\n"
                  "  P - Tx ping packet to ... \n"
                  "  Q - Transmit Mark(s)/Space(s)\n"
                  "  R - Enter Rx mode using current channel\n"
                  "  S - Display current settings\n"
                  "  T - Send Test Packets\n"
                  "  V - Toggle Verbose mode ON/OFF\n"
                  "  X - Exit\n"
                  "  Z - Specify Transmit Mode\n"
                  "  ? - Displays this message\n");

               HDiagRFTestHelp();
               break;
               }
         }

      c = DCDiagGetMenuCommand ("RF Mode");
      }

} /* End of diagRFTestMode */

#endif // !defined(CELL_SBS)

/*
** Note, this routine is not perfect when using the AT25 devices. If you are looking at the beginning of a section
** of flash memory, then it will be correct. But if you walk all the way through to the end of a 272 byte
** section things will then be wrong. This is because 272 byte page numbers do not match up properly with the 256
** byte page numbers we index the chip with.
**
** I don't see any reason to waste too much effort on making this perfect since this is not a production feature.
** This feature is strictly for engineering level debugging.
*/
void
diagDumpPage (unsigned int pageNum, unsigned int noDecrypt)
{
   unsigned int status;
   unsigned int encryptFlags;

      /* Clear out memory in case it doesn't read properly */
   memset (&DCSFPageBuffer, 0, sizeof(DCSFPageBuffer));

   encryptFlags = flashPageEncryptInfo (pageNum);
   status = DCSFReadProtectedPage (pageNum, &DCSFPageBuffer, SF_READ_CHKSUM | noDecrypt | encryptFlags);

      /* Dump out the contents */
   DiagPrintf ("DATAFLASH PAGE [%d] [%s]\n", pageNum, noDecrypt ? "Raw Data" : "Decrypted");
   DCDiagDumpHex (DCSFPageBufferDataPtr, 0, SFRunTimeData[SF_DATA_USER_DATA_SIZE], (unsigned char *)DCSFPageBufferInfoPtr, (encryptFlags & SF_NO_HEADER) ? 0 : HSF_HEADER_SIZE);

   if ((encryptFlags & SF_NO_HEADER))
      DiagPrintf ("CRC status is N/A\n");
   else
      DiagPrintf ("CRC status is %s\n", status ? "BAD" : "GOOD");
}

/*
** Serial Flash Testing
*/
void
diagFlashMode(void)
{
   unsigned char answer, pageEnc;
   unsigned int c, pageNum, pageNumEnd;

   while(1)
      {
      c = DCDiagGetMenuCommand ("Flash Mode");

      switch(c)
         {
         case 'B':
            {
            unsigned int iterations, eraseDelay, writeDelay, loopDelay;
            unsigned int temp, status, loopCount, index;
            unsigned int totalErrors;

               /* Set the default to the end of serial flash so someone doesn't accidentally overwrite memory that is more important */
            pageNum    = DCDiagGetInt32 ("Enter start page number: ", 0, SFRunTimeData[SF_DATA_MAX_PAGE] - 1, SFRunTimeData[SF_DATA_MAX_PAGE] - 16);
            pageNumEnd = DCDiagGetInt32 ("Enter end page number: ", pageNum, SFRunTimeData[SF_DATA_MAX_PAGE] - 1, SFRunTimeData[SF_DATA_MAX_PAGE] - 1);
            iterations = DCDiagGetInt32 ("Enter the number of erase/write iterations: ", 1, 500000UL, 1000);
            eraseDelay = DCDiagGetInt32 ("Enter the amount of delay (milliseconds) after each erase command: ", 0, 10000, 10);
            writeDelay = DCDiagGetInt32 ("Enter the amount of delay after each page write (milliseconds): ", 0, 10000, 10);
            loopDelay  = DCDiagGetInt32 ("Enter the delay between each iteration (seconds): ", 0, 3600 * 24U, 1);

         #ifdef SERIAL_FLASH_AT25
            #ifdef SERIAL_FLASH_AT45
               if (DCSFAT25Enabled)
            #endif
                  {
                     /*
                     ** Truncate start page back to 4K boundary and move end page to the last page of a 4K boundary.
                     */
                  pageNum &= ~(SF_AT25_PAGES_PER_BLOCK - 1);
                  pageNumEnd |= (SF_AT25_PAGES_PER_BLOCK - 1);
                  }
         #endif /* SERIAL_FLASH_AT25 */

            DiagPrintf("\nTesting pages %u through %u (Standard Serial flash requires 4K boundaries)\n", pageNum, pageNumEnd);
            answer = DCDiagGetChar ("Continue with flash test? (Y/N) ", "YN");
            DiagPrintf("Hit <ESC> to terminate test\n");
            loopCount = 1;
            totalErrors = 0;
            while (answer == 'Y')
               {
               if (DiagHit())
                  {
                  if (DiagIn() == ESCAPE_CHAR)
                     break;
                  }

               DiagPrintf("Iteration #%06u Error Count: %u\n", loopCount, totalErrors);

                  /* First step through all the pages and do an erase */
               for (temp = pageNum; temp <= pageNumEnd;)
                  {
                     /* Safety check, make sure we didn't just fall/jump into this loop */
                  if (diagnosticActive != DIAGNOSTIC_ACTIVE_FLAG)
                     {
                     SysAbort (__FILE__, __LINE__);
                     }

               #ifdef SERIAL_FLASH_AT25
                  #ifdef SERIAL_FLASH_AT45
                     if (DCSFAT25Enabled)
                  #endif
                        {
                        if ((status = DCAT25EraseBlock (temp * SF_AT25_BYTES_PER_PAGE, FALSE)) != 0)
                           {
                           DiagPrintf("Erase 4K block failed at %u returned error %X\n", temp, status);
                           totalErrors++;
                           }
                        temp += SF_AT25_PAGES_PER_BLOCK;
                        }
               #endif /* SERIAL_FLASH_AT25 */

               #ifdef SERIAL_FLASH_AT45
                  #ifdef SERIAL_FLASH_AT25
                     if (!DCSFAT25Enabled)
                  #endif
                        {
                        DCSFPageErase (temp);
                        temp++;
                        }
               #endif /* SERIAL_FLASH_AT45 */

                  if (eraseDelay)
                     Pend (0, eraseDelay);
                  }

                  /*
                  ** Now step through and write each page. We don't care what we write we just want to look for
                  ** an error when we write. I think it is best to keep writing random data to get maximum
                  ** variety.
                  */
               for (temp = pageNum; temp <= pageNumEnd; temp++)
                  {
                  for (index = 0; index < SFRunTimeData[SF_DATA_USER_DATA_SIZE]; index += sizeof(uint32_t))
                     {
                        /* Fill with pseudo random data, that is good enough */
                     put32 (DCSFPageBufferDataPtr + index, rand());
                     }

                  #ifdef SERIAL_FLASH_AT25
                     #ifdef SERIAL_FLASH_AT45
                        if (DCSFAT25Enabled)
                     #endif
                           {
                           status = DCAT25WritePage (temp * SF_AT25_BYTES_PER_PAGE, DCSFPageBufferDataPtr, SF_AT25_BYTES_PER_PAGE, SF_AES_CBC);
                           DCAT25ReadPage (temp * SF_AT25_BYTES_PER_PAGE, DCSFPageDataPtr ((DCSFPage *)diagBuffer), SF_AT25_BYTES_PER_PAGE, SF_AES_CBC);
                           }
                  #endif /* SERIAL_FLASH_AT25 */

                  #ifdef SERIAL_FLASH_AT45
                     #ifdef SERIAL_FLASH_AT25
                        if (!DCSFAT25Enabled)
                     #endif
                           {
                           status = !DCSFWritePageNoErase (temp, &DCSFPageBuffer);
                              /* Remove the encryption so we can compare below */
                           HSFCipherPage (SF_AES_CBC, temp * SFRunTimeData[SF_DATA_USER_PAGE_SIZE], (unsigned char *)&DCSFPageBuffer);

                           DCSFReadPage (temp, (DCSFPage *)diagBuffer, SF_READ_CHKSUM);
                           }
                  #endif /* SERIAL_FLASH_AT45 */

                  if (status)
                     {
                     DiagPrintf("Write page failed at %u returned error %X\n", temp, status);
                     }
                  else
                     {
                        /*
                        ** Standard serial flash we are actually reading back the data and verifying. With AT45 we are counting on the
                        ** chip to do a proper compare. Small chance that may not show a failure so lets do a full read and another
                        ** compare over here.
                        */
                     if (memcmp (DCSFPageBufferDataPtr, DCSFPageDataPtr ((DCSFPage *)diagBuffer), SFRunTimeData[SF_DATA_USER_DATA_SIZE]))
                        {
                        totalErrors++;
                        DiagPrintf("Read back test failed at %u\n", temp);
                        }
                     }

                  if (writeDelay)
                     Pend (0, writeDelay);
                  }

               if (loopDelay)
                  {
                  temp = MclockTimerGet() + loopDelay;
                  while (MclockTimerGet() != temp)
                     {
                     Pend (0, 250);

                     if (DiagHit())
                        {
                        if (DiagIn() == ESCAPE_CHAR)
                           {
                           answer = 'N';
                           break;
                           }
                        }
                     }
                  }

               if (loopCount == iterations)
                  break;
               loopCount++;
               }

            break;
            }

         case 'D':
            pageNum = DCDiagGetInt32 ("Enter page number: ", 0, SFRunTimeData[SF_DATA_MAX_PAGE] - 1, 0);
            pageEnc = DCDiagGetChar ("Display pages encrypted? (Y/N) ", "YN");

            if (pageEnc == 'Y')
               pageEnc = TRUE;
            else
               pageEnc = FALSE;

            answer = 'N';
            while (answer != 'Q')
               {
               diagDumpPage (pageNum, pageEnc ? SF_NO_DECRYPT : 0);

               answer = DCDiagGetChar ("\nNext Page? (N = Next, P = Prev, T = Toggle Decrypt, Q = Quit): ", "NPQT");
               if (answer == 'N')
                  pageNum++;
               else if (answer == 'P')
                  pageNum--;
               else if (answer == 'T')
                  pageEnc = !pageEnc;

               pageNum %= SFRunTimeData[SF_DATA_MAX_PAGE];
               }
            break;

         case 'E':
            pageNum    = DCDiagGetInt32 ("Enter start page number: ", 0, SFRunTimeData[SF_DATA_MAX_PAGE] - 1, 0);
            pageNumEnd = DCDiagGetInt32 ("Enter end page number: ", pageNum, SFRunTimeData[SF_DATA_MAX_PAGE] - 1, SFRunTimeData[SF_DATA_MAX_PAGE] - 1);

         #ifdef SERIAL_FLASH_AT25
            #ifdef SERIAL_FLASH_AT45
               if (DCSFAT25Enabled)
            #endif
                  {
                     /*
                     ** Truncate start page back to 4K boundary and move end page to the last page of a 4K boundary.
                     */
                  pageNum &= ~(SF_AT25_PAGES_PER_BLOCK - 1);
                  pageNumEnd |= (SF_AT25_PAGES_PER_BLOCK - 1);
                  DiagPrintf("Using 4K erase. Erasing pages %u through %u\n", pageNum, pageNumEnd);
                  answer = DCDiagGetChar ("Continue with page erase? (Y/N) ", "YN");
                  if (answer == 'Y')
                     {
                     DiagPrintf ("Erasing ");

                        /*
                        ** Erase all pages based on 4K boundaries
                        */
                     while (pageNum < pageNumEnd)
                        {
                           /* Safety check, make sure we didn't just fall/jump into this loop */
                        if (diagnosticActive != DIAGNOSTIC_ACTIVE_FLAG)
                           {
                           SysAbort (__FILE__, __LINE__);
                           }

                        DCAT25EraseBlock (pageNum * SF_AT25_BYTES_PER_PAGE, FALSE);

                           /* Place a '.' every 32K */
                        if ((pageNum & 0x70) == 0)
                           putch('.');

                        pageNum += SF_AT25_PAGES_PER_BLOCK;
                        }

                     DiagPrintf(" DONE\n");
                     }
                  break;
                  }
         #endif /* ifdef SERIAL_FLASH_AT25 */

         #ifdef SERIAL_FLASH_AT45
            #ifdef SERIAL_FLASH_AT25
               if (!DCSFAT25Enabled)
            #endif
                  {
                  unsigned char pageFillByte;

                  pageFillByte = DCDiagGetInt32 ("Enter page fill byte: ", 0, 255, 255);

                  answer = DCDiagGetChar ("Continue with page fill? (Y/N) ", "YN");
                  if (answer == 'Y')
                     {
                     DiagPrintf ("Filling ");

                        /*
                        ** Erase/fill all specified pages to the specified fill byte.
                        */
                     memset (diagBuffer, pageFillByte, SFRunTimeData[SF_DATA_USER_PAGE_SIZE]);
                     do
                        {
                        if ((pageNum & 0x3f) == 0)
                           putch('.');

                        DCSFWritePageBytes (pageNum++,  0, diagBuffer, SFRunTimeData[SF_DATA_USER_PAGE_SIZE]);
                        } while (pageNum <= pageNumEnd);

                     DiagPrintf(" DONE\n");
                     }
                  break;
                  }
         #endif /* ifdef SERIAL_FLASH_AT25 */

         case 'I':
            DebugPrintEvents (0, 0);
            break;

      #if defined(TEST_REV) && defined(FWDL_UART_SUPPORT)
         case 'F':
            {
            FWDL_UART_Hdr fwdl_uart_hdr;
            unsigned char result = FWDL_UART_OK; 
            unsigned char dev_type;
            unsigned char* ptr_data = s_aTmpDiagBuffer + FWDL_UART_HDR_LEN;
            
            FWDL_UART_FlushOut();
            
            while(1)
               {                      
               if ((result = FWDL_UART_WaitForPacket(s_aTmpDiagBuffer, &fwdl_uart_hdr)) == FWDL_UART_OK)
                  {        
                  if (fwdl_uart_hdr.flag & FWDL_UART_FIRST_PKT)
                     {
                     // Set the flash_target for 
                     initFlashDownloadParameters();
                     
                     //For meter firmware download, fails to read the meter table here to compare the part number
                     //Do the validation in normal mode after reboot
                        /*
                     dev_type = verifyFWDLPartNumber(ptr_data + NMP_PART_OFFSET);
                        */
                     dev_type = *(char *)(ptr_data + NMP_DEV_TYPE_OFFSET);
                     
                     *(char *)(ptr_data + NMP_STATUS_OFFSET) = FLASH_STATUS_ACTIVE;
                     }
                  
                  //Write the received data to flash
                  flashWrite(flash_target, ptr_data, fwdl_uart_hdr.data_len, fwdl_uart_hdr.offset);                  
                  }
               
               FWDL_UART_WriteBuff(&result, 1);
               
               if ((result == FWDL_UART_OK) && (fwdl_uart_hdr.flag & FWDL_UART_LAST_PKT))
                  {
                  if ( (dev_type == DEV_TYPE_METER_FIRMWARE) ||
                       (dev_type == DEV_TYPE_METER_RECONFIG) )
                     {
                     //Set the flags here
                     flash_download_target = flash_target;
                     SetMeterDownloadActive(DIP_METER_FIRMWARE|DIP_FIRMWARE_STATE0);
                     }
                  break;
                  }
               }
            GracefulShutdown(5, SOFT_SUICIDE_NMP_COMPLETE);
            }
            break;            
      #endif
            
      #if defined(TEST_REV) && defined(SERIAL_FLASH_AT45)
         case 'P':
         #ifdef SERIAL_FLASH_AT25
            #ifdef SERIAL_FLASH_AT45
               if (DCSFAT25Enabled)
            #endif
                  {
                  DiagPrintf("P command not supported for AT25 parts\n");
                  break;
                  }
         #endif /* SERIAL_FLASH_AT25 */
         #ifdef SERIAL_FLASH_AT45
            {
            unsigned int pageOffset;

            answer = 'A';
            pageNum = DCDiagGetInt32 ("Enter page number: ", 0, SFRunTimeData[SF_DATA_MAX_PAGE] - 1, 0);

            while (answer == 'A')
               {
               diagDumpPage (pageNum, 0);

               DiagPrintf ("Enter HEX offset from start of page: ");
               pageOffset = (unsigned int) DCDiagGetHexString (3, 0);
               DiagPrintf ("Enter HEX Byte: ");
               answer = (unsigned char) DCDiagGetHexString (2, 0);

                  /* Don't change if out of range, only support small page for this routine */
               if (pageOffset < SFRunTimeData[SF_DATA_USER_PAGE_SIZE])
                  {
                  DCSFPageBufferDataPtr[pageOffset] = answer;
                  DCSFWriteProtectedPage (pageNum, &DCSFPageBuffer, SF_AES_CBC);
                 }

               diagDumpPage (pageNum, 0);

               answer = DCDiagGetChar ("Quit? (A = Again, Q = Quit): ", "AQ");
               }
            break;
            }
         #endif /* #ifdef SERIAL_FLASH_AT45 */
      #endif /* #if defined(TEST_REV) && defined(SERIAL_FLASH_AT45) */

      #ifdef DEBUG_NVEVENT
         case 'A':
            nvMeterInit ();
            nvMeterArm ();
            memset (diagBuffer, 0x55, 64);
            nvTagWrite (TAG01_EVENT_MASK, diagBuffer, 64);
            break;

         case 'C':
            nvMeterEPFAlarm (TRUE, FALSE, 0);
            break;

         case 'Q':
            {
            DiagPrintf("\nRAM copy of EPF Page\n");
            nvMeterDataRead (diagBuffer, 0, nvEPFHeaderSize());
            DCDiagDumpHex (diagBuffer, 0, nvEPFHeaderSize(), NULL, 0);

            DiagPrintf("\nRAM copy of Interval Page\n");
            nvMeterIntervalDataRead (diagBuffer);
            DCDiagDumpHex (diagBuffer, 0, nvEPFIntervalSize(), NULL, 0);
            break;
            }

         case 'R':
            {
            unsigned int alarmNum;
            unsigned int len, i, copies;
            unsigned char buffer[16];

            alarmNum = DCDiagGetInt32 ("Enter an Alarm number: ", 0, 255, 0);
            copies = DCDiagGetInt32 ("Enter number of copies: ", 0, 20, 1);
            len = DCDiagGetInt32 ("Enter number of bytes to write: ", 1, 16, 1);

            for (i = 0; i < len; i++)
               {
               DiagPrintf ("Enter data byte #%u: ", i);
               buffer[i] = (unsigned char) DCDiagGetHexString (2, 0);
               }

            while (copies--)
               alarmFiresOff (alarmNum, "p", len, buffer);

            break;
            }

         case 'S':
            nvTestSetIndex ();
            break;
      #endif

      #ifdef DEBUG_NVTAG
         case 'T':
            nvTagDebugReport ();
            break;

         case 'U':
            {
            PLATFORMP pp;
            unsigned int length, i;
            unsigned int tag;

            tag = DCDiagGetInt32 ("Enter tag to write: ", 0, TAGNV_MAX_DEFINED, 0);
            length = DCDiagGetInt32 ("Enter bytes to write: ", 0, EVENT_MASK_TAG_LEN, 0);

            pp = PendTillGetPlatform();

            for (i = 0; i < length; i++)
               {
               DiagPrintf ("Enter tag byte #%u: ", i);
               ExportPP(pp)[i] = (unsigned char) DCDiagGetHexString (2, 0);
               }

            DiagPrintf ("Tag write is %s\n", nvTagWrite (tag, ExportPP(pp), length) ? "GOOD" : "BAD");;

            FreePlatform(pp);
            break;
            }
      #endif
         case 'V':
            {
            unsigned int tag;
            tag = DCDiagGetInt32 ("Enter tag to delete: ", 0, TAGNV_MAX_DEFINED, 0);
            nvTagDelete (tag);
            DiagPrintf("Tag %u deleted", tag);
            break;
            }

         case 'X':
            return;

         default:
            DiagPrintf ("Choose one of the following commands\n"
                  "  B - Flash Burn In Test\n"
                  "  D - Dump Page\n"
                  "  E - Erase/Fill Flash Pages\n"
               #if defined(TEST_REV) && defined(FWDL_UART_SUPPORT)
                  "  F - Firmware Download Testing\n"   
               #endif 
               #if defined(TEST_REV) && defined(SERIAL_FLASH_AT45)
                  "  P - Change a Byte\n"
               #endif
               #ifdef DEBUG_NVEVENT
                  "  A - ARM EPF and Init if necessary\n"
                  "  C - DisARM EPF\n"
                  "  Q - Dump Current (RAM) EPF and Alarm Page\n"
                  "  R - Force Alarm\n"
                  "  S - Change Index for Interval Data Page\n"
               #endif
               #ifdef DEBUG_NVTAG
                  "  T - Display NV Tag's or NV Tag Test Menu (AT25)\n"
                  "  U - Write NV Tag\n"
               #endif
                  "  V - Delete NV Tag\n"
                  "  X - Exit\n"
                  "  ? - Displays this message\n");
            break;
         }
      }

} /* End of diagFlashMode */

#if defined(S5_EV_LCE) || defined(S5_TOSHIBA_AP)
   unsigned int
   DCDiagGetRFPower (unsigned int oldVal, unsigned char *str)
   {
      unsigned int temp;

      DiagPrintf("Enter %s Power: ", str);
      temp = DCDiagGetInt32 ("", HRF_MIN_RF_POWER, HRF_MAX_RF_POWER, oldVal & 0xFF);
      DiagPrintf("Enter %s Power in dBm 10th's: ", str);

         /*
         ** For dBm value, we only have room to store a value from 5 (50) to 30.0 (300). So 5 or less will be
         ** considered 0, which also means the algorithm is off
         */
      oldVal >>= 8;
      if (oldVal)
         oldVal += RF_DBM_BASE;
      oldVal = DCDiagGetInt32 ("", 0, 300, oldVal);

      if (oldVal <= RF_DBM_BASE)
         oldVal = 0;
      else
         oldVal = (oldVal - RF_DBM_BASE) << 8;

      return (oldVal | temp);
   }
#endif  //S5_EV_LCE || S5_TOSHIBA_AP

/*
** This menu is for configuring things that the user shouldn't configure or wouldn't now
** like manufacturing calibration information. Everything is user configurable and should
** be done as a user would
*/
#ifdef SERIES5
   #define  CAL_CHAN0      "ADC0"
#else
   #define  CAL_CHAN0      "Temp"
#endif

void
diagConfigureMode(void)
{
   PLATFORMP pp;

   unsigned char c;
   unsigned char status = 0;
  #if defined(REGION_JAPAN)
   unsigned char c1;
  #endif

   pp = PendTillGetPlatform();

      /* If we don't have factory constants, go with the non-volatile copy for the LAN */
   if (HFCData.Dirty)
      memcpy (&HFCData.LAN, &RamLanSrc_M, sizeof(HFCData.LAN));

   while(1)
      {
      c = DCDiagGetMenuCommand ("Configure Mode");

      switch (c)
         {
#if !defined(CELL_SBS)
   #if !defined(ANALOG_CORRECTION_DISABLED)
         case 'A':
            {
            const unsigned char *str;
            CorrFac *cal;

            DiagPrintf("Select scale/offset <0=%s, 1=ADC1, 2=ADC2, 3=ADC3, 4=RFPower, 5=RSSI>", CAL_CHAN0);
            c = (unsigned char) DCDiagGetInt32 (": ", 0, 5, 0);

            switch (c)
               {
               case 0:
                  str = CAL_CHAN0;
                  cal = &HFCData.TempFac;
                  break;

               case 1:
                  str = "ADC1";
                  cal = &HFCData.ADC1Fac;
                  break;

               case 2:
                  str = "ADC2";
                  cal = &HFCData.ADC2Fac;
                  break;

               case 3:
                  str = "ADC3";
                  cal = &HFCData.ADC3Fac;
                  break;

               case 4:
                  str = "RFPower";
                  cal = &HFCData.RampPACorrFac;
                  break;

               case 5:
                  str = "RSSI";
                  cal = &HFCData.RSSIFac;
                  break;
               }

            sprintf ((char*) ExportPP(pp), "Enter %s Scale: ", str);
            cal->Scale = (unsigned int) DCDiagGetInt32 ((char*) ExportPP(pp), 0, 65535L, cal->Scale);
            sprintf ((char*) ExportPP(pp), "Enter %s Offset: ", str);
            cal->Offset = (signed int) DCDiagGetInt32 ((char*) ExportPP(pp), -32767L, 32767L, cal->Offset);

            status = DCFCWrite();

         #ifdef TEMPERATURE_SUPPORTED
               /* If messing with calibration constants, reset our temperature in case they changed that one */
            temperature = (signed char)readAtoD(DAC_TEMP);
         #endif
            break;
            }
   #endif /* if !defined(ANALOG_CORRECTION_DISABLED) */
#endif // !defined(CELL_SBS)

         case 'B':
            DiagPrintf("Old Config Bytes = C1=0x%02X, C2=0x%02X, C3=0x%02X, C4=0x%02X, C5=0x%02X, C6=0x%02X\n",
                  FirstConfigByte_M, SecondConfigByte_M,
                  ThirdConfigByte_M, FourthConfigByte_M,
                  RamTransparentConfig3_M, SixConfigByte_M);

            DiagPrintf ("Enter C1(HEX): ");
            FirstConfigByte_M = (unsigned int) DCDiagGetHexString (2, FirstConfigByte_M);
            DiagPrintf ("Enter C2(HEX): ");
            SecondConfigByte_M = (unsigned int) DCDiagGetHexString (2, SecondConfigByte_M);
            DiagPrintf ("Enter C3(HEX): ");
            ThirdConfigByte_M = (unsigned int) DCDiagGetHexString (2, ThirdConfigByte_M);
            DiagPrintf ("Enter C4(HEX): ");
            FourthConfigByte_M = (unsigned int) DCDiagGetHexString (2, FourthConfigByte_M);
            DiagPrintf ("Enter C5(HEX): ");
            RamTransparentConfig3_M = (unsigned int) DCDiagGetHexString (2, RamTransparentConfig3_M);
            DiagPrintf ("Enter C6(HEX): ");
            SixConfigByte_M = (unsigned int) DCDiagGetHexString (2, SixConfigByte_M);

            eeUpdateImage();
            DiagPrintf("New Config Bytes = C1=0x%02X, C2=0x%02X, C3=0x%02X, C4=0x%02X, C5=0x%02X, C6=0x%02X\n",
                   FirstConfigByte_M, SecondConfigByte_M,
                   ThirdConfigByte_M, FourthConfigByte_M,
                   RamTransparentConfig3_M, SixConfigByte_M);
            break;

         case 'C':
#if !defined (JUPITER_MESH_SUPPORTED) && !defined (WISUN_FAN_SUPPORT)
             DiagPrintf("Old CRC Adder = 0x%04X\n", RamCRCAdder_M);

             DiagPrintf ("Enter CRC Adder (HEX): ");
             RamCRCAdder_M = (unsigned short int) DCDiagGetHexString (4, RamCRCAdder_M);
             HFCData.NetworkID = RamCRCAdder_M;

             eeUpdateImage();
             status = DCFCWrite();

             DiagPrintf("New CRC Adder = 0x%04X\n", RamCRCAdder_M);
             break;
#else
             {
                 DiagPrintf("JM/WISUN : Old CRC Adder =  %s\n", g_stPersist4e.m_aNetId);
                 DiagPrintf ("JM/WISUN : Enter CRC Adder : ");
                 uint8_t buffer[MAX_SSID_LEN+1] ={0};
                 DCDiagGetAsciiString (MAX_SSID_LEN, buffer,(char const*)g_stPersist4e.m_aNetId);
                 PERSIST_Load (PERSIST_STRUCT_4E);
                 strcpy((char*)g_stPersist4e.m_aNetId,(char const*)buffer);
                 PERSIST_Save (PERSIST_STRUCT_4E);
                 DiagPrintf("JM/WISUN : New CRC Adder = %s\n", g_stPersist4e.m_aNetId);
                 break;
             }
#endif

#if !defined(CELL_SBS)
         case 'D':
           #if defined(REGION_JAPAN)
            rf_config4e( FALSE );
           #else
            rf_config4e( TRUE );
           #endif //REGION_JAPAN
            break;
#endif // !defined(CELL_SBS)

         case 'E':
         #ifdef CELL_SBS
            if (!debugToMeterSwitch)
            { // if not being controlled in pass through mode
         #endif
         #if !defined(DIAG_LOCK_CMD) || !defined(CELL_SBS)
               /* This should put the device into OPEN mode, no DCW integrity */
            defaultConfigBytes (CONFIG_LEVEL_6);  //FIXME AES_Audit() fails ???
         #else // DIAG_LOCK_CMD
            {
              unsigned char StartingLock = RamSecurityConfig_M[1];

              /* This should put the device into OPEN mode, no DCW integrity */
              defaultConfigBytes (CONFIG_LEVEL_6);
              // changing config forces init value into diag lock, replace starting value
              sramChangeChar(StartingLock, &(RamSecurityConfig_M[1]));
              /*  Update non-volatile memory  */
              eeUpdateImage();
            }
         #endif // DIAG_LOCK_CMD
            DiagPrintf("Encryption is off\n");
         #ifdef CELL_SBS
            }
            else
            {
            DiagPrintf("This function is disabled on meter port\n");
            }
         #endif
            break;

         case 'F':
#if defined(CELL_SBS)
            PERSIST_Load(PERSIST_STRUCT_APP);
            DiagPrintf ("HES0 IP Address:\n");
            DiagPrintf ("Enter HEX Byte 1: ");
            g_stPersistApp.m_aAppDstIp6Addr[0] = (unsigned char)DCDiagGetHexString(2, g_stPersistApp.m_aAppDstIp6Addr[0]);
            DiagPrintf ("Enter HEX Byte 2: ");
            g_stPersistApp.m_aAppDstIp6Addr[1] = (unsigned char)DCDiagGetHexString(2, g_stPersistApp.m_aAppDstIp6Addr[1]);
            DiagPrintf ("Enter HEX Byte 3: ");
            g_stPersistApp.m_aAppDstIp6Addr[2] = (unsigned char)DCDiagGetHexString(2, g_stPersistApp.m_aAppDstIp6Addr[2]);
            DiagPrintf ("Enter HEX Byte 4: ");
            g_stPersistApp.m_aAppDstIp6Addr[3] = (unsigned char)DCDiagGetHexString(2, g_stPersistApp.m_aAppDstIp6Addr[3]);
            g_stPersistApp.m_unAppSrcPort = (unsigned short)DCDiagGetInt32("Enter HES Server Port (0-65535): ", 0, 65535, g_stPersistApp.m_unAppSrcPort);
            g_stPersistApp.m_unAppDstPort = (unsigned short)DCDiagGetInt32("Enter HES Client Port (0-65535): ", 0, 65535, g_stPersistApp.m_unAppDstPort);
            PERSIST_Save(PERSIST_STRUCT_APP);

            if (!nvTagRead(TAG15_LARGE_DCW, s_aTmpDiagBuffer, (TAG_HEADER_SIZE + 2)))
            { // tag not there
              DiagPrintf ("Tag 15 not initialized, NVRAM may be defective\n");
            }
            else
            {
              DiagPrintf("Tag 15 size = %d, version = %d\n", s_aTmpDiagBuffer[1], get16(&s_aTmpDiagBuffer[4]));
              if ((get16(&s_aTmpDiagBuffer[4]) != CELL_TABLE202_VERSION) || (s_aTmpDiagBuffer[1] != (sizeof(CELLNET_TABLE_202_PARTIAL) + 2)))
              { // if not the correct version and size
                DiagPrintf ("Can't update Cellnet 202 Table because tag table is wrong version or size\n");
              }
              else
              {
                CELLNET_TABLE_202_PARTIAL *Cell202TablePtr = (CELLNET_TABLE_202_PARTIAL *)&s_aTmpDiagBuffer[6];
                uint16_t tmpVariable;
                uint16_t Index;

                nvTagRead(TAG15_LARGE_DCW, s_aTmpDiagBuffer, (TAG_HEADER_SIZE + 2 + sizeof(CELLNET_TABLE_202_PARTIAL)));
                DiagPrintf ("HES1 IP Address:\n");
                DiagPrintf ("Enter HEX Byte 1: ");
                Cell202TablePtr->m_HES1Ip6Addr[0] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_HES1Ip6Addr[0]);
                DiagPrintf ("Enter HEX Byte 2: ");
                Cell202TablePtr->m_HES1Ip6Addr[1] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_HES1Ip6Addr[1]);
                DiagPrintf ("Enter HEX Byte 3: ");
                Cell202TablePtr->m_HES1Ip6Addr[2] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_HES1Ip6Addr[2]);
                DiagPrintf ("Enter HEX Byte 4: ");
                Cell202TablePtr->m_HES1Ip6Addr[3] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_HES1Ip6Addr[3]);
                put16((unsigned char *)&tmpVariable, Lget16((unsigned char *)&Cell202TablePtr->m_TCPListenPort));
                tmpVariable = (unsigned short)DCDiagGetInt32("Enter TCP Server Listen Port (0-65535): ", 0, 65535, tmpVariable);
                put16((unsigned char *)&Cell202TablePtr->m_TCPListenPort, Lget16((unsigned char *)&tmpVariable));
                DiagPrintf ("NTP0 IP Address:\n");
                DiagPrintf ("Enter HEX Byte 1: ");
                Cell202TablePtr->m_NTP0Ip6Addr[0] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_NTP0Ip6Addr[0]);
                DiagPrintf ("Enter HEX Byte 2: ");
                Cell202TablePtr->m_NTP0Ip6Addr[1] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_NTP0Ip6Addr[1]);
                DiagPrintf ("Enter HEX Byte 3: ");
                Cell202TablePtr->m_NTP0Ip6Addr[2] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_NTP0Ip6Addr[2]);
                DiagPrintf ("Enter HEX Byte 4: ");
                Cell202TablePtr->m_NTP0Ip6Addr[3] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_NTP0Ip6Addr[3]);
                DiagPrintf ("NTP1 IP Address:\n");
                DiagPrintf ("Enter HEX Byte 1: ");
                Cell202TablePtr->m_NTP1Ip6Addr[0] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_NTP1Ip6Addr[0]);
                DiagPrintf ("Enter HEX Byte 2: ");
                Cell202TablePtr->m_NTP1Ip6Addr[1] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_NTP1Ip6Addr[1]);
                DiagPrintf ("Enter HEX Byte 3: ");
                Cell202TablePtr->m_NTP1Ip6Addr[2] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_NTP1Ip6Addr[2]);
                DiagPrintf ("Enter HEX Byte 4: ");
                Cell202TablePtr->m_NTP1Ip6Addr[3] = (unsigned char)DCDiagGetHexString(2, Cell202TablePtr->m_NTP1Ip6Addr[3]);
                #pragma diag_suppress=Pa039
                put16((unsigned char *)&tmpVariable, Lget16((unsigned char *)&Cell202TablePtr->m_HBServerPort));
                tmpVariable = (unsigned short)DCDiagGetInt32("Enter HB Server Port (0-65535): ", 0, 65535, tmpVariable);
                put16((unsigned char *)&Cell202TablePtr->m_HBServerPort, Lget16((unsigned char *)&tmpVariable));
                put16((unsigned char *)&tmpVariable, Lget16((unsigned char *)&Cell202TablePtr->m_HBClientPort));
                tmpVariable = (unsigned short)DCDiagGetInt32("Enter HB Client Port (0-65535): ", 0, 65535, tmpVariable);
                put16((unsigned char *)&Cell202TablePtr->m_HBClientPort, Lget16((unsigned char *)&tmpVariable));
                put16((unsigned char *)&tmpVariable, Lget16((unsigned char *)&Cell202TablePtr->m_HBRate));
                tmpVariable = (unsigned short)DCDiagGetInt32("Enter HB Rate in seconds (10-65535): ", 10, 65535, tmpVariable);
                put16((unsigned char *)&Cell202TablePtr->m_HBRate, Lget16((unsigned char *)&tmpVariable));
                #pragma diag_default=Pa039
                Cell202TablePtr->m_HBFailoverThreshold = (unsigned char)DCDiagGetInt32("Enter HB Failover Threshold count (0-100): ",
                                                                                  0,
                                                                                  100,
                                                                                  Cell202TablePtr->m_HBFailoverThreshold);
                DiagPrintf("Enter APN Value (ESC to view CRC): ");
                Index = 0;
                char aTmpDiagBufferAPN[sizeof(Cell202TablePtr->m_APN)];
                do
                {
                   aTmpDiagBufferAPN[Index] = DiagIn();
                   if (((aTmpDiagBufferAPN[Index] == 0x0D) || (aTmpDiagBufferAPN[Index] == 0x0A)) ||
                        (Index == (sizeof(Cell202TablePtr->m_APN) - 1)))
                   { // if got the end-of-line, or if filled up buffer
                      if (Index)
                      { // if anything was entered besides the end of line
                        memset(&Cell202TablePtr->m_APN[0], 0, sizeof(Cell202TablePtr->m_APN));
                        memcpy(&Cell202TablePtr->m_APN[0], &aTmpDiagBufferAPN[0], Index);
                      }
                      break;
                   }
                   if ((aTmpDiagBufferAPN[Index] < '!') || (aTmpDiagBufferAPN[Index] > '~'))
                   {
                      break;   // only ASCII printable chars allowed
                   }
                } while (Index++ < sizeof(Cell202TablePtr->m_APN));

                DiagPrintf("\nAPN CRC: %04X\n", UnRefCalcCrc(&Cell202TablePtr->m_APN[0], sizeof(Cell202TablePtr->m_APN), 0));
                nvTagWrite(TAG15_LARGE_DCW, &s_aTmpDiagBuffer[4], (sizeof(CELLNET_TABLE_202_PARTIAL) + 2));

                if (!nvTagRead(TAG51_CELL_OTHER_CONFIG, s_aTmpDiagBuffer, (TAG_HEADER_SIZE + 2)))
                { // tag not there
                  DiagPrintf ("Tag 51 not initialized, NVRAM may be defective\n");
                }
                else
                {
                  DiagPrintf("Tag 51 size = %d, version = %d\n", s_aTmpDiagBuffer[1], get16(&s_aTmpDiagBuffer[4]));
                  if ((get16(&s_aTmpDiagBuffer[4]) != CELL_OTHER_CONFIG_VERSION) || (s_aTmpDiagBuffer[1] != (sizeof(CELL_OTHER_CONFIG_TABLE) + 2)))
                  { // if not the correct version and size
                    DiagPrintf ("Can't update TAG51 because tag table is wrong version or size\n");
                  }
                  else
                  {
                    CELL_OTHER_CONFIG_TABLE *CellOtherConfigPtr = (CELL_OTHER_CONFIG_TABLE *)&s_aTmpDiagBuffer[6];

                    nvTagRead(TAG51_CELL_OTHER_CONFIG, s_aTmpDiagBuffer, (TAG_HEADER_SIZE + 2 + sizeof(CELL_OTHER_CONFIG_TABLE)));
                    DiagPrintf ("Enter HEX Band Mask: ");
                    put32((unsigned char *)&CellOtherConfigPtr->m_BandMask,
                          DCDiagGetHexString(8, get32((unsigned char *)&CellOtherConfigPtr->m_BandMask)));
                    nvTagWrite(TAG51_CELL_OTHER_CONFIG, &s_aTmpDiagBuffer[4], (sizeof(CELL_OTHER_CONFIG_TABLE) + 2));
                  }
                }
              }
            }
#else // defined(CELL_SBS)
            PERSIST_Load (PERSIST_STRUCT_APP);

            if (c_aPersistData[PERSIST_STRUCT_APP].m_pDefaultDataStruct)
            {
               DiagPrintf("\nAPP settings -- ");
               if ((DCDiagGetChar ("Load DEFAULTS? (Y/N): ", "YyNn") & 0xDF) == 'Y')
               {
                  memcpy(c_aPersistData[PERSIST_STRUCT_APP].m_pDataStruct, c_aPersistData[PERSIST_STRUCT_APP].m_pDefaultDataStruct,
                         c_aPersistData[PERSIST_STRUCT_APP].m_ucDataStructLen);
               }
            }

            UpdateIPv6PortAddress("APP", &g_stPersistApp.m_unAppSrcPort, &g_stPersistApp.m_unAppDstPort, g_stPersistApp.m_aAppDstIp6Addr);
            UpdateIPv6PortAddress("F/W download", &g_stPersistApp.m_unFwDlSrcPort, &g_stPersistApp.m_unFwDlDstPort, g_stPersistApp.m_aFwDlDstIp6Addr);
            UpdateIPv6PortAddress("NMS", &g_stPersistApp.m_unNMSSrcPort, &g_stPersistApp.m_unNMSDstPort, g_stPersistApp.m_aNMSDstIp6Addr);

            g_stPersistApp.m_unNMSStatsDstPort = DCDiagGetInt32("Enter Comm Stats Port (0-65535): ", 0, 65535, g_stPersistApp.m_unNMSStatsDstPort);
            PERSIST_Save(PERSIST_STRUCT_APP);
#endif // defined(CELL_SBS)
            break;

      #ifdef USE_DIAG_LOCK
               /*
               ** Allows user to toggle the DIAG mode override bit.
               ** Note!  Bit is ACTIVE LOW so a value of '0' means the DIAG
               ** port is locked and a value of '1' means the override is enabled.
               */
         case 'G':
            RamSecurityConfig_M[1] ^= SECURITY_01_DIAG_MODE_LOCK_OVERRIDE;
            /*  Update non-volatile memory  */
            eeUpdateImage();
            DiagPrintf ("DIAG override is %s\n",
                    (ee_config_shadow.sec_config[1] & SECURITY_01_DIAG_MODE_LOCK_OVERRIDE) ? "Set" : "Clear");
            break;
      #endif // USE_DIAG_LOCK

         case 'H':
            c = (unsigned char) DCDiagGetInt32 ("Enter Hardware Type (0-255): ",
                                     0, 255, HFCData.MinorHardwareType);

               /* don't update anything unless the user changed the hardware type */
            if (c != HFCData.MinorHardwareType)
               {
            #ifdef CPU_ATMEL_CORTEXM4_4C
                  /*
                  ** If switching hardware types on the RF Module, don't reset all the manufacturing variables since the
                  ** board will likely be built as an RF module and at a later date will be combined with a host board
                  ** where the hardware ID will then change.
                  */
               if ((features_supported & FEATURE_RFMODULE) && (HFCData.MinorHardwareType != 0))
                  {
                  HFCData.MinorHardwareType = c;
                  DCFCWrite();
                  }
               else
            #endif
                  {
                  HFCData.MinorHardwareType = c;
                  status = DCFCReset(HFCData.MinorHardwareType);
                  }

                  /*
                   ** Make sure defaults are correct for this radio type.
                   ** But don't lose a factory loaded DCW
                  */
                  eeGetandValidate(EE_RESET_FIRMWARE);
                  }
            break;

#ifdef FIELD_TOOL
         case 'I':
            /*
            ** Has the user activated an activity timeout on this device? Purpose of
            ** this is to keep the user from accidentally turning on the device and
            ** then allowing it to run down the battery. So the default for these
            ** devices is to automatically shut off if they go more than 20 minutes
            ** without any activity on the LPP port.
            **
            ** The Battery Dropout voltage field will be used for this feature. If this
            ** field is 255 then the feature is disabled. This is consistent with how
            ** this field is used for battery devices. For any other value, the
            ** timeout then is in minutes where there is a minimum of 5 minutes
            */
            RamBatteryDropoutVal_M = (unsigned int) DCDiagGetInt32("Enter Battery inactivity timer in minutes 5-255 (255 disables inactivity timer): ", 5, 255, RamBatteryDropoutVal_M);
            eeUpdateImage();

            if (RamBatteryDropoutVal_M == UCHAR_MAX)
              DiagPrintf("Battery inactivity timer is disabled\n");
            else
              DiagPrintf("Battery inactivity timer is %d minutes\n",RamBatteryDropoutVal_M);
            break;
#endif

         case 'L':
            DiagPrintf ("Enter HEX Byte 1: ");
            HFCData.LAN[0] = (unsigned char) DCDiagGetHexString (2, HFCData.LAN[0]);
            DiagPrintf ("Enter HEX Byte 2: ");
            HFCData.LAN[1] = (unsigned char) DCDiagGetHexString (2, HFCData.LAN[1]);
            DiagPrintf ("Enter HEX Byte 3: ");
            HFCData.LAN[2] = (unsigned char) DCDiagGetHexString (2, HFCData.LAN[2]);
            DiagPrintf ("Enter HEX Byte 4: ");
            HFCData.LAN[3] = (unsigned char) DCDiagGetHexString (2, HFCData.LAN[3]);

            memcpy (&RamLanSrc_M, &HFCData.LAN, sizeof(HFCData.LAN));

               /*
               ** Now that we have a LAN address, assuming it is not 0, set
               ** out the operational bit so the radio will run.
               */
            if (get32 ((unsigned char *)&RamLanSrc_M))
               FirstConfigByte_M |= OPERATIONAL;

               /*
               ** Write the LAN address to the serial emulation block and to the
               ** factory constants block.
               */
            eeUpdateImage();
            status = DCFCWrite();
            break;

      #if !defined(CELL_SBS)
        #if !defined(WISUN_FAN_SUPPORT)
         case 'M':
            RamChanMask_M = (unsigned int) DCDiagGetInt32("Enter Channel Mask (0-255): ", 0, 255, RamChanMask_M) & ~CHAN_RESERVED;

            if( RamChanMask_M == REG_CODE_TEST_ALL || RamChanMask_M == REG_CODE_EUROPE )
            {
               /*
                * Note: For REG_CODE_TEST_ALL and REG_CODE_EUROPE we want the ability to
                * modify the channel mask from the default. In Europe, certain sub-regions have
                * slightly different masks. We will use g_stPersist4e.m_aucChannelMask to save the
                * modified mask and load it (instead of the default) in RegCode_Init().
                */
               PERSIST_Load (PERSIST_STRUCT_4E);
               getPersistBitmap( g_stPersist4e.m_aucChannelMask, 0 );

               if( RamChanMask_M == REG_CODE_EUROPE )
               {
                  memset( &g_stPersist4e.m_aucChannelMask[8], 0, 24 );
               }
               else
               {
                  getPersistBitmap( &g_stPersist4e.m_aucChannelMask[8], 1 );
                  memset( &g_stPersist4e.m_aucChannelMask[16], 0, 16 );
               }

               PERSIST_Save (PERSIST_STRUCT_4E);
            }
            if( RegCode_Init(&g_stPersist4e) != 0 )
            {
               DiagPrintf("Unknown Channel Mask, defaulting to 0 - North America\n");
            }
            eeUpdateImage();
            rf_displayChannelMask();
            break;
        #endif // !WISUN_FAN_SUPPORT

        #ifdef REGION_JAPAN
         case 'N':
            {
            unsigned int len, index;

            c1 = 0xdf &
                  DCDiagGetChar ("\nWhich Region? (C = Config Parameters, B = B000, D = Big DCW) ", "CcBbDd");
            if (c1 == ESCAPE_CHAR)
               break;

            if (c1 == 'C')
               DumpHex ((unsigned char *)&ee_ram_copy, 0, 110);
            else if (c1 == 'B')
               {
               ExportEEROMBuffer (s_aTmpDiagBuffer, 0, EEROM_BUFFER_LEN);
               DumpHex (s_aTmpDiagBuffer, 0, EEROM_BUFFER_LEN);
               }
            else if (c1 == 'D')
               {
               flashRead (FLASH_3, s_aTmpDiagBuffer, 256, 0);
               len = (*((unsigned short *)s_aTmpDiagBuffer) & 0x7fff) + 6;
               index = 0;

               if (len > BIG_DCW_BUFFER_SIZE)
                  len = BIG_DCW_BUFFER_SIZE;

                  /* Keep it simple and just dump out to the nearest 256 byte boundary */
               while (index < len)
                  {
                  DumpHex (s_aTmpDiagBuffer, index, 256);
                  index += 256;
                  flashRead (FLASH_3, s_aTmpDiagBuffer, 256, index);
                  }
               }
            break;
            }
        #endif //REGION_JAPAN

         // RF Power
         case 'P':
            {
         #if defined(S5_EV_LCE) || defined(S5_TOSHIBA_AP)
            HFCData.RFPower[0] = DCDiagGetRFPower (HFCData.RFPower[0], "MIN");
            HFCData.RFPower[1] = DCDiagGetRFPower (HFCData.RFPower[1], "Middle");
            HFCData.RFPower[2] = DCDiagGetRFPower (HFCData.RFPower[2], "MAX");

            #ifdef RF_ONE_WATT
               HFCData.RFPower[3] = DCDiagGetRFPower (HFCData.RFPower[3], "1W");
            #endif

               /*
               ** Record temperature of unit when RF power levels were entered. Give user the option
               ** to skip this. This is to allow engineers to change things and then be able to restore
               ** them. Or manufacturing can always enter a specific value if that works.
               */
            temperature = (signed char)readAtoD(DAC_TEMP);
            DiagPrintf("Power calibration temperature (measured %dC)", temperature);
            if (!HFCData.MfgTemp)
               HFCData.MfgTemp = temperature;
            HFCData.MfgTemp = (signed char)DCDiagGetInt32 (": ", -40, 85, HFCData.MfgTemp);
         #endif //defined(S5_EV_LCE) || defined(S5_TOSHIBA_AP)

         #if defined(SERIES5_JP) || defined(SERIES5_JP_EP)
            int tempDiagActiveFlag = diagnosticActive;
            // Set diagnosticActive so that these min, mid, and max power settings can be saved to flash
            diagnosticActive = DIAGNOSTIC_ACTIVE_FLAG;
            DiagPrintf("Power range is %d to %d\n",HRF_MIN_RF_POWER,HRF_MAX_RF_POWER);
         #endif //SERIES5_JP
         #if defined(SERIES5) || defined(SILICON_LABS_SOC) || defined(SERIES5_JP) || defined(SERIES5_JP_EP)
            HFCData.RFPower[0] = (uint16_t) DCDiagGetInt32 ("Enter MIN Power: ", HRF_MIN_RF_POWER, HRF_MAX_RF_POWER, HFCData.RFPower[0]);
            HFCData.RFPower[1] = (uint16_t) DCDiagGetInt32 ("Enter Middle Power: ", HRF_MIN_RF_POWER, HRF_MAX_RF_POWER, HFCData.RFPower[1]);
            HFCData.RFPower[2] = (uint16_t) DCDiagGetInt32 ("Enter MAX Power: ", HRF_MIN_RF_POWER, HRF_MAX_RF_POWER, HFCData.RFPower[2]);

            #ifdef RF_ONE_WATT
               HFCData.RFPower[3] = (uint16_t) DCDiagGetInt32 ("Enter 1W Power: ", HRF_MIN_RF_POWER, HRF_MAX_RF_POWER, HFCData.RFPower[3]);
            #endif
         #endif
			/* NB: The Si4467 REX4 S5Sx33 Build uses none of the above power value settings as they vary with each 2 MHz group of channels. */

            RamMaxPower_M = (unsigned char) DCDiagGetInt32 ("Enter OUTPUT Power: ", 0,
                                                         #ifdef RF_ONE_WATT
                                                            3,
                                                         #else
                                                            2,
                                                         #endif
                                                            RamMaxPower_M);
            eeUpdateImage();
            status = DCFCWrite();
      #if defined(SERIES5)
         #ifdef SERIES5_HW_PLATFORM
            // ??? the following LNA flags is not used anywhere, keeping the dummy input in case somone is looking ???
            (void) (unsigned char) DCDiagGetInt32 ("Enter LNA flags: ", 0, 5, 0);
         #endif // SERIES5_HW_PLATFORM
      #endif //SERIES5

         #if defined(SERIES5_JP) || defined(SERIES5_JP_EP)
            int outpwr;
			outpwr = PORTING_GetScaledRFPower(RamMaxPower_M);
            // Do some calculations to simply print out what the power setting is now.
            DiagPrintf("Power set to ");
            if (RamMaxPower_M==0)
               DiagPrintf("MIN");
            else if (RamMaxPower_M==1)
               DiagPrintf("MID");
            else
               DiagPrintf("MAX");

            DiagPrintf(": %d ",outpwr);
            outpwr = (10*((outpwr & 0x3F)+1)/2)-180;  //Equation from CC1200 Users Guide PA_CFG1 Register Description
            DiagPrintf("(%d.%d dBm)\n", outpwr/10,abs(outpwr%10));

            // Restore diagnosticActive to what it was before this configuration.
            diagnosticActive = tempDiagActiveFlag;
         #endif //SERIES5_JP

			L4G_RF_InitTxPower();  // All new Power settings are validated in one place in the respective L4G_RF layer.
            break;
            }
      #else // !defined(CELL_SBS)

         case 'M':
            {
            uint16_t Index = 0;
            unsigned char aTmpDiagBufferMSN[sizeof(fc_data.MSN)];

            DiagPrintf("Enter MSN Value (CR to view current MSN): ");

            Index = GetString(aTmpDiagBufferMSN, sizeof(aTmpDiagBufferMSN), TRUE);
            if (Index)
            { // if something typed in
               memcpy(fc_data.MSN, aTmpDiagBufferMSN, sizeof(fc_data.MSN));
               status = DCFCWrite();
            }
            DiagPrintf("\nMSN = %s\n\n", fc_data.MSN);
            }
            break;
         case 'N':
         case 'P':
            notSupported();
            break;
      #endif // !defined(CELL_SBS)

      #if defined(SILICON_LABS_SOC) || defined(COLLECTOMETER_SUPPORTED)
         case 'Q':
            {
            #if defined(SILICON_LABS_SOC)
               bool apcEnable = RamApcDisabled_M ? FALSE : TRUE;
               apcEnable = (bool) DCDiagGetInt32("Enter APC on/off (0/1): ", 0, 1, apcEnable);
               if (apcEnable == FALSE)
               {
                  ee_ram_copy.config_7 |= APC_DISABLE;
               }
               else
               {
                  ee_ram_copy.config_7 &= ~APC_DISABLE;
                  RamApcSnrThreshold1_M = (uint8_t) DCDiagGetInt32("Enter APC SNR Threshold1: ", 0, 255, RamApcSnrThreshold1_M);
                  RamApcSnrThreshold2_M = (uint8_t) DCDiagGetInt32("Enter APC SNR Threshold2: ", RamApcSnrThreshold1_M + 1, 255, RamApcSnrThreshold2_M);
                  RamApcSnrThreshold3_M = (uint8_t) DCDiagGetInt32("Enter APC SNR Threshold3: ", RamApcSnrThreshold2_M + 1, 255, RamApcSnrThreshold3_M);
               }
            #endif   // defined(SILICON_LABS_SOC)

            #if defined(COLLECTOMETER_SUPPORTED)
               bool gwModeEnable = (bool) DCDiagGetInt32("\nCollector Mode on/off (0/1): ", 0, 1, IsCollectorRadio());
               SetCollectorRadioMode( gwModeEnable );
            #endif
               eeUpdateImage();
            }
            break;
      #endif // defined(SILICON_LABS_SOC) || defined(COLLECTOMETER_SUPPORTED)

         case 'R':
               /*
               ** Keep this one different than the hardware ID version. Here we do want to
               ** make the DCW switch back to its defaults if someone does run this.
               */
            eeGetandValidate(EE_RESET_SHOP_VIRGINIZE_DCW);  //FIXME AES_Audit() fails
         #if defined(SERIES5_HW_PLATFORM)
            g_stPersistApp.m_aFlags[1] = 0;                                     // LNA
            PERSIST_Save( PERSIST_STRUCT_APP );
         #endif // SERIES5_HW_PLATFORM
         #if defined(CELL_SBS)
            Check202Initialized();
         #endif // defined(CELL_SBS)
            DiagPrintf("All users parameters have been reset to their defaults\n");
            break;

         case 'S':
            {
            unsigned int group;
            group = (unsigned int) DCDiagGetInt32 ("Enter Soft Id (0-65535): ",
                                     0, 65535, (RamGroupingHigh_M << 8) | RamGroupingLow_M);
            RamGroupingHigh_M = (unsigned char)(group >> 8);
            RamGroupingLow_M = (unsigned char)group;
            eeUpdateImage();
            break;
            }
         case 'T':
#if defined(SERIES5) || defined(SERIES5_JP_EP) || defined(SERIES5_JP)
            HFCData.TCXOError = (signed int) DCDiagGetInt32 ("Enter TCXO Error adjustment: ",
                                     -32767L, 32767L, HFCData.TCXOError);
            status = DCFCWrite();
#else
            notSupported();
#endif // defined(SERIES5) || defined(SERIES5_JP_EP) || defined(SERIES5_JP)
            break;

#ifdef ELSTER_REX4
         case 'U':
            DiagPrintf ("Enter REX4 XO Tune Value: ");
            fc_data.MfgTbl.rfXOTune = (unsigned char) DCDiagGetHexString (2,fc_data.MfgTbl.rfXOTune);

               /* Force the new value to get used */
            Si4467ControlInit();

            status = DCFCWrite();
            break;
#else

         case 'U':
         #if defined(REGION_JAPAN)
            DiagPrintf ("Enter HEX Byte 1: ");
            RamOUI0_M = (unsigned char) DCDiagGetHexString(2, RamOUI0_M);
            DiagPrintf ("Enter HEX Byte 2: ");
            RamOUI1_M = (unsigned char) DCDiagGetHexString(2, RamOUI1_M);
            DiagPrintf ("Enter HEX Byte 3: ");
            RamOUI2_M = (unsigned char) DCDiagGetHexString(2, RamOUI2_M);
            eeUpdateImage();
         #else //REGION_JAPAN
           /*
           This menu is used for Time zone setting in Brazil, can be used for US as well.
           Usually along with GMT offset DST flag is also set/cleared. DST Flag is part
           of Configuration byte 2.
           - Cuurent implementation
           if want to change DST flag then make sure you set it in Configuration byte 2
           using option 'B'
           */
            DiagPrintf ("Enter GMTOffset(HEX): ");
            RamGMTOffset_M = (unsigned int) DCDiagGetHexString (2, RamGMTOffset_M);
            eeUpdateImage();
         #endif //REGION_JAPAN
            break;
#endif
#ifdef WISUN_FAN_SUPPORT
         case 'W':
            rf_configWiSun();
            break;
#endif // WISUN_FAN_SUPPORT

#if defined(CELL_SBS)
         case 'W':
            {
            uint8_t s_aTmpDiagBuffer[TAG_HEADER_SIZE + 2 + sizeof(CELLNET_TABLE_202_PARTIAL)];
            CELLNET_TABLE_202_PARTIAL *Cell202TablePtr = (CELLNET_TABLE_202_PARTIAL *)&s_aTmpDiagBuffer[TAG_HEADER_SIZE + 2];
            uint16_t *pRandomOutage = (uint16_t *)&s_aTmpDiagBuffer[TAG_HEADER_SIZE];
            uint16_t OutageValue = EPF_DEFAULT_MSG_DELAY / 1000;

            // Get the random startup delay limit
            nvTagRead(TAG15_LARGE_DCW, s_aTmpDiagBuffer, (TAG_HEADER_SIZE + 2 + sizeof(CELLNET_TABLE_202_PARTIAL)));
            Cell202TablePtr->m_CellStartupWait = (unsigned char)DCDiagGetInt32("Enter random startup delay limit (0-255) minutes: ",
                                                                               0,  255, Cell202TablePtr->m_CellStartupWait);
            nvTagWrite(TAG15_LARGE_DCW, &s_aTmpDiagBuffer[TAG_HEADER_SIZE], (sizeof(CELLNET_TABLE_202_PARTIAL) + 2));

            // Get the random outage delay limit
            if (nvTagRead(TAG53_RNDM_OUTAGE_DELAY, s_aTmpDiagBuffer, (sizeof(uint16_t) + TAG_HEADER_SIZE)))
            { // if TAG53 is there
               OutageValue = *pRandomOutage;
            }
            *pRandomOutage = (uint16_t)DCDiagGetInt32("Enter random outage delay limit (0-65535) seconds: ",
                                                       0,  65535, OutageValue);
            nvTagWrite(TAG53_RNDM_OUTAGE_DELAY, (unsigned char *)pRandomOutage, sizeof(uint16_t));
            }
            break;
#endif // CELL_SBS

         case 'X':
               /* Only way out is here */
            FreePlatform(pp);
            return;

#if defined(TEMPERATURE_SUPPORTED) && defined(SERIES5)
         case 'Y':
           {
             int8_t cTemperature;
             if( L4G_RF_ReadTemperature(&cTemperature) == FALSE )
             {
               DiagPrintf("Unable to read temperature\n");
             }
             else
             {
                signed int nLastRead = cTemperature;
               DiagPrintf( "Current calibrated temperature %d\n", cTemperature + (HFCData.TempFac.Offset >> 8) );
                HFCData.TempFac.Offset = (DCDiagGetInt32 ("Enter environment temperature (degrees C, 10 units accepted offset): ", nLastRead-10, nLastRead+10, nLastRead) - nLastRead) << 8;
                status = DCFCWrite();
             }
           }
           break;
#endif  // TEMPERATURE_SUPPORTED & SERIES5

         case 'Z':
            {
            printf("LAN ID = %4H\n", HFCData.LAN);
#if !defined(CELL_SBS)
#ifdef ELSTER_REX4
            unsigned char index = (g_unLastChannel / 10);  // convert RF channel to the 2MHz block of interest with 200 kHz channels
            DiagPrintf("RF Power PA Values: %02x %02x %02x,  2MHz Index: %d,  Level Selected: %u\n",fc_data.MfgTbl.RFPower21dBM[index],
                fc_data.MfgTbl.RFPower24dBM[index],fc_data.MfgTbl.RFPower27dBM[index],index,RamMaxPower_M);
#endif
#if defined(SERIES5) || defined(SERIES5_JP) || defined(SERIES5_JP_EP)
            DiagPrintf("Max power = %d, Output power = %d\n", HFCData.RFPower[2] , RamMaxPower_M);  //Warning: Expected message parsed by Mfg. Eng. SW
#endif
#if defined(S5_EV_LCE) || defined(SILICON_LABS_SOC) || defined(S5_TOSHIBA_AP)
            DiagPrintf("RF Power PA Values: %02x %02x %02x", HFCData.RFPower[0] & 0xFF, HFCData.RFPower[1] & 0xFF, HFCData.RFPower[2] & 0xFF);  //Warning: Expected message parsed by Mfg. Eng. SW
         #ifdef RF_ONE_WATT
            DiagPrintf(" %02x", HFCData.RFPower[3] & 0xFF);
         #endif
            DiagPrintf(", Level Selected: %u\n", RamMaxPower_M);
#endif
         #if !defined(ANALOG_CORRECTION_DISABLED)
            DiagPrintf("TCXO Error = %d\n", HFCData.TCXOError);
            DiagPrintf("%s Scale= %u, Offset = %d\n", CAL_CHAN0, HFCData.TempFac.Scale, HFCData.TempFac.Offset);
            DiagPrintf("ADC1 Scale= %u, Offset = %d\n", HFCData.ADC1Fac.Scale, HFCData.ADC1Fac.Offset);
            DiagPrintf("ADC2 Scale= %u, Offset = %d\n", HFCData.ADC2Fac.Scale, HFCData.ADC2Fac.Offset);
            DiagPrintf("ADC3 Scale= %u, Offset = %d\n", HFCData.ADC3Fac.Scale, HFCData.ADC3Fac.Offset);
            DiagPrintf("RFPower Scale= %u, Offset = %d\n", HFCData.RampPACorrFac.Scale, HFCData.RampPACorrFac.Offset);
            DiagPrintf("RSSI Scale= %u, Offset = %d\n", HFCData.RSSIFac.Scale, HFCData.RSSIFac.Offset);
         #endif // ANALOG_CORRECTION_DISABLED

            DiagPrintf("Hardware Type = %u\n", HFCData.MinorHardwareType);
#endif // !defined(CELL_SBS)
#ifndef SERIES5 /*#warning fixme for SERIES5*/
            unsigned int crc, len;
            crc = MDCWResident (&len);
            DiagPrintf("Big DCW Length = %u, CRC = %04x\n", len, crc);
#endif
           #if defined(COM_ADAPTER) || defined(FIELD_TOOL_ADAPTER)
            memcpyReverse( g_oEUI64, g_stPersistMnf.m_unMAC, MAC_ADDR_LEN );
            DiagPrintf("MAC ADDR= %02X %02X %02X %02X %02X %02X %02X %02X (RAM addr=%X=0x%0llu)\n",
                   g_stPersistMnf.m_unMAC[0],g_stPersistMnf.m_unMAC[1],g_stPersistMnf.m_unMAC[2],g_stPersistMnf.m_unMAC[3],
                   g_stPersistMnf.m_unMAC[4],g_stPersistMnf.m_unMAC[5],g_stPersistMnf.m_unMAC[6],g_stPersistMnf.m_unMAC[7],
                   &g_u64MAC, g_u64MAC );
           #endif  // COM_ADAPTER
            if (status)
               DiagPrintf("\nFAIL: Factory constants write error!\n");
            else if (HFCData.Dirty)
               DiagPrintf("\nFAIL: Factory constants have not been initialized!\n");
            }
            break;

         default:
            DiagPrintf ("Choose one of the following commands\n"
               #if !defined(ANALOG_CORRECTION_DISABLED) && !defined(CELL_SBS)
                  "  A - A2D Scales and Offsets\n"
               #endif
                  "  B - Change Config Bytes\n"
                  "  C - CRC Adder\n"
    #if !defined(CELL_SBS)
                  "  D - 4e config (deprecated)\n"
    #endif
                  "  E - Disable Encryption\n"
    #if defined(CELL_SBS)
                  "  F - Set HES0/1 IP, HES Ports, NTP0/1 IP, HB values, and APN\n"
    #else
                  "  F - Set IP Addresses and Ports\n"
    #endif // defined(CELL_SBS)
    #ifdef USE_DIAG_LOCK
      #ifndef CELL_SBS
                  "  G - Toggle DIAG override\n"
      #else // CELL_SBS
                  "  G - Lock DIAG Mode\n"
      #endif // CELL_SBS
    #endif  // USE_DIAG_LOCK
                  "  H - Hardware Type\n"
    #ifdef FIELD_TOOL
                  "  I - Disable/Enable Inactivity Timer (Battery Saver)\n"
    #endif
                  "  L - Change LAN ID\n"
    #ifdef CELL_SBS
                  "  M - MSN\n"
    #else // CELL_SBS
      #ifndef WISUN_FAN_SUPPORT
         #ifdef JUPITER_MESH_SUPPORTED
                  "  M - RF Chan Mask (Ch 31 is configurable for JM)\n"
         #else
                  "  M - RF Chan Mask\n"
         #endif   /* JUPITER_MESH_SUPPORTED */
      #endif   /* WISUN_FAN_SUPPORT */
                  "  P - RF Power\n"
      #if defined(SILICON_LABS_SOC)
                  "  Q - APC/Collector Mode Config\n"
      #elif defined(COLLECTOMETER_SUPPORTED)
                  "  Q - Collector Mode Config\n"
      #endif
    #endif // CELL_SBS
                  "  R - Reset Parameters to Manufacturing Defaults\n"
                  "  S - Soft ID\n"
    #if defined(SERIES5) || defined(SERIES5_JP_EP) || defined(SERIES5_JP)
                  "  T - TCXO Error\n"
    #endif // defined(SERIES5) || defined(SERIES5_JP_EP) || defined(SERIES5_JP)
    #ifdef GRIDSTREAM_SBS
                  "  U - Set Time Zone\n"
    #endif
    #ifdef WISUN_FAN_SUPPORT
                  "  W - WiSUN config\n"
    #endif
    #ifdef CELL_SBS
                  "  W - Random startup/outage delays\n"
    #endif // CELL_SBS
                  "  X - Exit\n"
#if defined(TEMPERATURE_SUPPORTED) && defined(SERIES5)
                  "  Y - Calibrate temperature\n"
#endif  // TEMPERATURE_SUPPORTED && SERIES5
                  "  Z - Dump Manufacturing Configuration\n"
                  "  ? - Displays this message\n");
            break;
      }
   }
} // End of configuration mode

/*
** Routine to dump data in Motorola S-Record format out the serial port. Not much use anymore so
** this is a candidate to dump
*/
#define MOT_START          1
#define MOT_MIDDLE         2
#define MOT_END            3
#define MOT_ALL            4
#define MOT_HDR_SIZE       (5L)
#define MOT_DATA_SIZE      (32L)
#define TMP_BUFF_SIZE      (MOT_DATA_SIZE + MOT_HDR_SIZE + 1L)
#define HEX_BUFF_SIZE      (TMP_BUFF_SIZE * 2L) + 1L
unsigned char
DumpMOT(unsigned long image, unsigned long base, unsigned long size, unsigned char state)
{
   unsigned char *tempbuff;
   unsigned char *hexbuff;
   PLATFORMP   pp;
   unsigned int j, k;
   unsigned char chksum, c;
   unsigned long addr;
   unsigned long count;
   unsigned long offset;

   offset = 0L;

      /* Use a platform for a buffer */
   pp = PendTillGetPlatform ();
   tempbuff = ExportPP(pp);
   hexbuff = tempbuff + TMP_BUFF_SIZE;

   if ((state == MOT_START) || (state == MOT_ALL))
      DiagPrintf("S00B00005554494C494E455486\n");

      /* Write the srecords for the image */
   while (size)
      {
         /* Compute the number of bytes for the next record */
      count = min (size, MOT_DATA_SIZE);

         /* Clear the buffers */
      memset (tempbuff, 0, TMP_BUFF_SIZE);
      memset (hexbuff, 0, HEX_BUFF_SIZE);

         /* Create the length byte in the binary s-record */
      tempbuff[0] = (unsigned char)(count + MOT_HDR_SIZE);

         /* Create the address byte in the s-record */
      addr = base + offset;
      ReverseBytes(&addr, sizeof(unsigned long));
      memcpy (&tempbuff[1], ((unsigned char*)&addr), sizeof(unsigned long));

         /* Copy the binary data to the srecord */
      memcpy (&tempbuff[MOT_HDR_SIZE], (unsigned char FAR*)image, (unsigned int)count);

         /* Compute the check sum and add it to the s-record */
      chksum = CalcChkSum (tempbuff, (unsigned int)(count + MOT_HDR_SIZE));
      tempbuff[(unsigned int)(count+MOT_HDR_SIZE)] = chksum - 1;

      k = (unsigned int)((count + MOT_HDR_SIZE + 1));
      for (j = 0; j < k; j++)
         sprintf ((char *) &hexbuff[j << 1], "%02X", tempbuff[j]);

         /* Print the s-record to the file */
      DiagPrintf("S3%s\n", (char FAR *) hexbuff);
      size -= count;
      offset += count;
      image += count;

         /* Stop if the user hits CTRL-C */
      if (DiagHit())
         {
         c = DiagIn();
         if (c == 0x03)
            {
            DiagPrintf("ERROR: Operator interrupted\n");
            FreePlatform (pp);
            return (FALSE);
            }
         }
      }

   if ((state == MOT_END) || (state == MOT_ALL))
      DiagPrintf("S70500000000FA\n");

   FreePlatform(pp);
   return(TRUE);
}

/*
** Simple function to be consistent about how ADC data is displayed. This routine purposely displays raw data only.
*/
void
DCDiagAtoDChannel (unsigned int ch)
{
   unsigned int raw;

   raw = HADCGet(ch);

   /*gsk:UTS parses the resolution number in brackets to interpret ADC data.As the conversion result is scaled upto 12 bits for less-than-12 bit ADCs ,use BITS_PER_SAMPLE instead of ADC_NUM_BITS to match the data*/
   DiagPrintf ("Channel %2u = %04u (%u-Bit Data)\n", ch, raw, ADC_RAW_BITS);
}


/*
** diagnosticMain ()
**
** This is the entry point to the diagnostic functions. This is the
** main menu that you initially see. This menu is mainly used to
** get to other sub-menu's.
*/
void diagnosticMain (void)
{
   int c;

   while(1)
      {
#ifdef USE_HOT_KEY_DIAG
        hotkey( DCDiagGetString, &diagHKcontext );
#else /* USE_HOT_KEY_DIAG */
      c = DCDiagGetMenuCommand ("Diagnostic Mode");

   #ifdef   DIAGNOSTIC_SELFTEST
      if (diagSelftestActive())
         {
         button_state = !BUTTON_STATE;
         diag_j_last_stable = button_last_stable = SysTimeGet();
         diag_j_state = (Jumpers() & JUMPER_1);

         selftest_state = SELFTEST_START;
         diagRFTestMode ('D');
         }
   #endif

      switch(c)
         {
      #ifdef BLUETOOTH_UART
         case 'B':
            HBluetoothDiagMenu();
            break;
      #endif

         case 'C':
            diagConfigureMode();
            break;
         case 'F':
            diagFlashMode();
            break;
         case 'I':
            DebugPrintEvents (0, 0);
            break;
         case 'K':
            diagSecurityMode();
            break;
         case 'M':
            HDiagManufacturingTestMode();
            break;

#if !defined(CELL_SBS)
         case 'R':
            diagRFTestMode ('D');
            break;

       #ifdef ROUTEB_STUB
        extern void toggle_routeb_stub();
        case 'S':
            toggle_routeb_stub();
            break;  // Route-B stub enabler
       #endif // ROUTEB_STUB
#endif // !defined(CELL_SBS)

       //#ifndef GRIDSTREAM_SBS
         case 'T':
               /*
               ** This is for older products that still use 9600 as the diagnostic baud rate
			   ** Supressed for SBS to prevent PEBKAC accidents.
               */
            flush_debug_out();
            debugUART_init (38400U);
            break;
       //#endif

         case 'X':
            return;

         case 'Z':
            flush_debug_out();
            hardReboot();
            return;

         default:
            DiagPrintf ("Choose one of the following commands\n"
               #ifdef BLUETOOTH_UART
                  "  B - Bluetooth\n"
               #endif
                  "  C - Configure Mode\n"
                  "  F - Flash Mode\n"
                  "  I - Display Events\n"
                  "  K - Security Mode\n"
                  "  M - Manufacturing Test\n"
               #if !defined(CELL_SBS)
                  "  R - RF Test Mode\n"
               #endif // !defined(CELL_SBS)
               #ifdef ROUTEB_STUB
                  "  S - Route-B Stub Toggle\n"
               #endif // ROUTEB_STUB
                  "  T - Set baud rate to 38400\n"
                  "  X - Exit\n"
                  "  Z - REBOOT\n"
                  "  ? - Displays this message\n");
            break;
         }
#endif /* USE_HOT_KEY_DIAG */
      }

} /* End of diagnosticMain */


/*
** [TASK]
**
** Diagnostic mode is a task and it is also a special mode in the radio. If the radio starts off
** in diagnostic mode then currently only Task0 and a crippled Ticker task are also running. This
** allows diagnostic mode to have full control of RF resources without having to worry about
** other task interactions.
*/
void DiagnosticTask(void)
{
      /*
      ** Since you can't use "sizeof" as part of a #if, you are forced to then use C
      ** code to sanity check the size of something. But with the current ARM compiler
      ** at least, the following does not actually produce any code because the compiler
      ** is smart enough to realize the statement is false.
      **
      ** Net result, use these checks all you want because they shouldn't actually add
      ** code unless someone did something wrong and obviously that won't be in the
      ** release code if it is wrong.
      **
      ** If something else bigger wants to use the diagBuffer than please add to the
      ** sanity check.
      */
   if (DIAG_BUFFER_SIZE < sizeof(DCSFPage))
      {
      DiagPrintf("DIAG_BUFFER_SIZE must be bigger\n");
      SysAbort (__FILE__, __LINE__);
      }
      /*
      ** Grab a buffer for diagnostic mode. We don't start all tasks, so there should be a chunk
      ** available. For now, make sure the buffer is bigger than a full serial flash page.
      */
   diagBuffer = LGAPP_MALLOC(DIAG_BUFFER_SIZE);//nearAlloc(DIAG_BUFFER_SIZE);

#if !defined(CELL_SBS)

   //L4E_MIB_Init();  // Allocates Memory for Channel Maps, only call once.
   setDefaultRFparam();

#else // !defined(CELL_SBS)

   setupNVProtection();

   #ifdef   ENCRYPT_NV_MEMORY
   // Make sure we have our encryption key setup and available before we proceed
   while (!verifyNVProtection())
   {
      Pend (0, 2);
      RetrieveRnd();
   }
   #endif   // ENCRYPT_NV_MEMORY

   Check202Initialized();

   #if defined(HOLDUP_POWER)
   setHoldupCharge (POWER_SAVE_EPF, TRUE);    // setup first
   features_supported &= ~FEATURE_HOLDUP;     // off by default in diagnostic
   #ifdef SCAP_CHRG_PIN
   SET_PIO_LO(SCAP_CHRG_PIN);
   #endif
   #ifdef SCAP_DISCHRG_PIN
   SET_PIO_LO(SCAP_DISCHRG_PIN);
   #endif
   #endif  // HOLDUP_POWER

#endif // !defined(CELL_SBS)
      /*
      ** This is a task, so there is no exit. If the user tries to exit we just go right back
      ** to the main menu.
      */
   while (1)
      {
      diagnosticMain ();

      Pend (0, 20);
      }
}

void UpdateIPv6PortAddress(char *str, uint16_t *src_port, uint16_t *dest_port, unsigned char *pIPv6)
{
unsigned int Index;

 DiagPrintf("Enter %s ports and IPv6 address:\n", str);

   *src_port  = DCDiagGetInt32("  Src  Port (0-65535): ", 0, 65535, *src_port);
   *dest_port = DCDiagGetInt32("  Dest Port (0-65535): ", 0, 65535, *dest_port);

   for (Index = 0; Index < 8; Index++)
   {
      DiagPrintf("  IPv6 HEX Group[%d]: ", Index);
      put16(pIPv6+(Index*2), (unsigned int) DCDiagGetHexString(4, get16(pIPv6+(Index*2))));
   }
}

#if defined(GRIDSTREAM_SBS) && defined(TEST_REV) && !defined(REGION_JAPAN) && !defined(METER_REXU_SUPPORTED)

   // Encrypted default PANA seed for non-REXU radios - only available in DEV builds
   const uint8_t ENCRYPTED_PANA_SEED_FOR_NON_REX4_RADIOS[AES_256_BYTE_COUNT] =
   {
      0x8C,0xA9,0xE0,0xA3,0x54,0xA8,0x48,0x82,0x1C,0xC1,0x0F,0xB2,0x5E,0xCE,0xEE,0x72,
      0x60,0xE6,0xDA,0xC2,0x79,0x1F,0x08,0x9B,0x2B,0x92,0x90,0x23,0x78,0xD3,0xF8,0x5D
   };
#endif

void diagSecurityMode(void)
{
#if !(defined(SERIES5_JP_EP) || defined(SERIES5_JP) || defined(REGION_JAPAN)) || defined(FIELD_TOOL_ADAPTER)
   unsigned char  c;
   unsigned int   crc;
   uint8_t pana_key[AES_256_BYTE_COUNT];

   while(1)
   {
      memset( pana_key, 0, AES_256_BYTE_COUNT );
      c = DCDiagGetMenuCommand ("Security Mode");

      switch (c)
      {
      #ifdef PSEMX_SUPPORTED
         case 'E':
         {
            unsigned char enablePsemx;
            unsigned char psemx_control = 0;
            PLATFORMP    pp = NULLPP;

            GetPsemxConfigByte (&psemx_control);

            enablePsemx =  0xdf & DCDiagGetChar  ("Use PSEMX Protocol? (Y/N) ", "YyNn");
            if(enablePsemx == 'Y')
            {
               psemx_control = PSEMX_ENABLED_BIT | PSEMX_ONE_TIME_BIT;
               pp = PendTillGetPlatform();
               memset (ExportPP(pp), 0, sizeof(PSEMX_CONF) + TAG_HEADER_SIZE);

               PSEMX_Conf_Load(ExportPP(pp));
               PSEMX_CONF *psemx_conf = (PSEMX_CONF *) (ExportPP(pp) + TAG_HEADER_SIZE);

               //PSEMX uses FULL user to talk with USeries meter
               Lput16((void *)psemx_conf->user_id, SECURITY_USER_FULL);

               DiagPrintf("\nEnter PSEMX User Password: ");
               if (DCDiagSecurityGetHexString(PSEMX_PWD_LEN, s_aTmpDiagBuffer, FALSE))
               {
                 memcpy (psemx_conf->user_pwd_key[SECURITY_USER_FULL].password, s_aTmpDiagBuffer, PSEMX_PWD_LEN);
               }
               DiagPrintf("\nPSEMX User Password CRC: %04X\n", UnRefCalcCrc(&psemx_conf->user_pwd_key[SECURITY_USER_FULL].password, PSEMX_PWD_LEN, 0));

               DiagPrintf("\nEnter PSEMX User Key: ");
               if (DCDiagSecurityGetHexString(PSEMX_KEY_LEN, s_aTmpDiagBuffer, FALSE))
               {
                 memcpy (psemx_conf->user_pwd_key[SECURITY_USER_FULL].key, s_aTmpDiagBuffer, PSEMX_KEY_LEN);
               }
               DiagPrintf("\nPSEMX User Key CRC: %04X\n", UnRefCalcCrc(&psemx_conf->user_pwd_key[SECURITY_USER_FULL].key, PSEMX_KEY_LEN, 0));

               PSEMX_Conf_Save(psemx_conf);

               FreePlatform(pp);
               DiagPrintf("\n");
            }
            else
            {
               psemx_control = 0;
            }

            SetPsemxConfigByte (&psemx_control);
         }
         break;
      #endif  //PSEMX_SUPPORTED

         case 'M':
         {
            DiagPrintf("Enter Meter Password Value(ESC to view): ");
            crc = 0;
            do
            {
               s_aTmpDiagBuffer[crc] = DiagIn();

               // if got input and the end-of-line
               if ((s_aTmpDiagBuffer[crc] == 0x0D) || (s_aTmpDiagBuffer[crc] == 0x0A))
               {
                  break;
               }

               if ((s_aTmpDiagBuffer[crc] < '!') || (s_aTmpDiagBuffer[crc] > '~'))
               {
                  crc = 0;
                  break;   // only ASCII printable chars allowed
               }

            } while (crc++ < 20);

            if (crc)
            {
               SetupMeterPassword (crc, s_aTmpDiagBuffer);
            }

            ExportEEROMBuffer (s_aTmpDiagBuffer, 12, 20);
            DiagPrintf("\nMeter Password: %s\n", s_aTmpDiagBuffer);
         }
         break;

   #if defined(CELL_SBS)
         case 'N':
            crc = 0;       // using "crc" as a scrap variable
            memset(s_aTmpDiagBuffer, 0, (CELLULAR_USERNAME_MAX_SIZE + TAG_HEADER_SIZE + 1)); // clear buffer to ensure string is terminated.
            DiagPrintf("Enter Cell Username (CR to clear, ESC to view value): ");
            do
            {
              s_aTmpDiagBuffer[crc] = DiagIn();

              // If ESC pressed, just exit
              if (s_aTmpDiagBuffer[crc] == 0x1B)
              {
                break;
              }
              else if ((s_aTmpDiagBuffer[crc] == 0x0D) || (s_aTmpDiagBuffer[crc] == 0x0A))
              {
                // Check if we must clear the user name
                if (!crc)
                {
                  nvTagDelete(TAG50_CELLULAR_USERNAME);
                }
                else
                {
                  nvTagWrite(TAG50_CELLULAR_USERNAME, s_aTmpDiagBuffer, crc);
                }
                break;
              }
              else
              {
                // Valid char entered.
              }
            } while (crc++ < CELLULAR_USERNAME_MAX_SIZE);

            memset(s_aTmpDiagBuffer, 0, (CELLULAR_USERNAME_MAX_SIZE + TAG_HEADER_SIZE + 1)); // clear buffer to ensure string is terminated.
            if (nvTagRead(TAG50_CELLULAR_USERNAME, s_aTmpDiagBuffer, CELLULAR_USERNAME_MAX_SIZE + TAG_HEADER_SIZE))
            {
              DiagPrintf("\nCell Username = %s\n\n", &s_aTmpDiagBuffer[TAG_HEADER_SIZE]);
            }
            else
            {
              DiagPrintf("\nNo Cell Username present.\n\n");
            }

            DiagPrintf("Enter Cell Password Value (CR to clear, ESC to view CRC): ");
            crc = 0;       // using "crc" as a scrap variable
            memset(s_aTmpDiagBuffer, 0, (CELLULAR_1N_PWD_MAX_SIZE + TAG_HEADER_SIZE + 1)); // clear buffer to ensure string is terminated.
            do
            {
               s_aTmpDiagBuffer[crc] = DiagIn();

               if ( ((crc >= CELLULAR_1N_PWD_MIN_SIZE) && ((s_aTmpDiagBuffer[crc] == 0x0D) || (s_aTmpDiagBuffer[crc] == 0x0A))) ||
                     (crc == CELLULAR_1N_PWD_MAX_SIZE) )
               { // if (got minimum size and the end-of-line) or maximum size
                  nvTagWrite(TAG36_CELLULAR_PASSWORD, s_aTmpDiagBuffer, crc);
                  break;
               }

               if ((s_aTmpDiagBuffer[crc] < '!') || (s_aTmpDiagBuffer[crc] > '~'))
               {
                  break;   // only ASCII printable chars allowed
               }
            } while (crc++ < CELLULAR_1N_PWD_MAX_SIZE);

            if ((s_aTmpDiagBuffer[crc] == 0x0D) || (s_aTmpDiagBuffer[crc] == 0x0A))
            {
              // Check if we must clear the password
              if (!crc)
              {
                nvTagDelete(TAG36_CELLULAR_PASSWORD);
              }
            }

            // hide the actual data and display just the CRC of the password
            if (nvTagRead(TAG36_CELLULAR_PASSWORD, s_aTmpDiagBuffer, TAG_HEADER_SIZE))
            {
              DiagPrintf("\n\nCell Password CRC: %04X\n", get16(s_aTmpDiagBuffer+2));
            }
            else
            {
              DiagPrintf("\n\nCell Password not present");
            }
            break;

         case 'O':
            // Read and display the current value and options
            if (nvTagRead(TAG55_CELL_AUTH_TYPE, s_aTmpDiagBuffer, TAG_HEADER_SIZE + sizeof(uint16_t)))
            {
               crc = *((uint16_t *)&s_aTmpDiagBuffer[TAG_HEADER_SIZE]);
            }
            else
            {
               // Set the default to CHAP
               crc = CELL_AUTH_CHAP;
               *((uint16_t *)&s_aTmpDiagBuffer[TAG_HEADER_SIZE]) = crc;
            }
            printf("Choose one of the following options:\n"
            "  0 - None\n"
            "  1 - PAP\n"
            "  2 - CHAP\n"
            "> %d", crc);

            debug_out('\b');
            crc = DiagIn();

            if (crc != '\r')
            {
               while ( (crc > '2') || (crc < '0') )
               {
                  printf("Invalid option. Select 0, 1 or 2\n");
                  printf("> ");
                  crc = DiagIn();
               }
               debug_out(crc);
               // Set the new value
               *((uint16_t *)&s_aTmpDiagBuffer[TAG_HEADER_SIZE])= crc - '0';

            }
            else
            {
              // Just print out the current value
               debug_out(*((uint16_t *)&s_aTmpDiagBuffer[TAG_HEADER_SIZE]) + '0');
            }

            // Write the selection to NVM
            nvTagWrite(TAG55_CELL_AUTH_TYPE, &s_aTmpDiagBuffer[TAG_HEADER_SIZE], sizeof(uint16_t));
            break;
   #endif // defined(CELL_SBS)

   #if defined(USE_PANA)
      #if defined(METER_REXU_SUPPORTED) || defined(TEST_REV)
         case 'F':
         #if defined(METER_REXU_SUPPORTED)
               /*
               ** Derive PANA key, even if LAN Id is not present (still zeroes), this doesn't look good but
               ** it's to match REX4 behavior with other SBS radios.
               */
               REXU_DerivePANAKey();
         #else
               DeriveDefaultPANAKey(ENCRYPTED_PANA_SEED_FOR_NON_REX4_RADIOS);
         #endif
            break;
      #endif  // METER_REXU_SUPPORTED

         case 'S':
            DiagPrintf("Enter PANA Seed (ESC to view CRC's): ");
            if (DCDiagSecurityGetHexString(AES_256_BYTE_COUNT, s_aTmpDiagBuffer, FALSE))
            {
               if (memcmp(s_aTmpDiagBuffer, pana_key, AES_256_BYTE_COUNT) == 0)
               {
                  nvTagDelete(TAG33_PANA_PRESHARED_KEY);
                  DiagPrintf("\nRemoving PANA Key\n");
               }
               else
               {
                  DerivePANAKey(s_aTmpDiagBuffer, &pana_key[0], FALSE);
               }
            }
            else
            {
               PrintPANAKeyCRC();
            }
            break;
   #endif // defined(USE_PANA)

        case 'X':
           return;

        default:
            DiagPrintf("Choose one of the following commands\n"
                  "  M - Meter Password\n"
   #if defined(CELL_SBS)
                  "  N - Cell Username and Password\n"
                  "  O - Cell Authentication Type\n"
   #endif
   #ifdef PSEMX_SUPPORTED
                  "  E - PSEMX Configuration\n"
   #endif
   #if defined(USE_PANA)
      #if defined(METER_REXU_SUPPORTED) || defined(TEST_REV)
                  "  F - Derive PANA key with fixed PANA Seed\n"
      #endif
                  "  S - Enter PANA Seed\n"
   #endif // defined(USE_PANA)
                  "  X - Exit\n"
                  "  ? - Displays this message\n");
      }
   }
#elif REGION_JAPAN
   SecurityMode();
#endif //!REGION_JAPAN
} // End of security menu



void printHexData( const unsigned char * p_pData, unsigned int p_unDataLen )
{
    unsigned int i;

    DiagPrintf( "Data:");

    for (i = 0; i < p_unDataLen; i++)
    {
        if( !(i & 0x3F) )
            DiagPrintf( "\n" );
        DiagPrintf(" %02X", p_pData[i] );
    }
    DiagPrintf( "\n" );
}

void getPersistTimeslot( PERSIST_4E_TIMESLOT * p_pTimeslot )
{
    uint32_t tmp;

    p_pTimeslot->m_ucAlignament = 0xFF;
    p_pTimeslot->m_ucID = DCDiagGetInt32 ("Enter ID (0-255): ", 0, 255, p_pTimeslot->m_ucID);
    p_pTimeslot->m_unCCAOffset = DCDiagGetInt32 ("Enter CCA Offset (0-65535): ", 0, 65535, p_pTimeslot->m_unCCAOffset);
    p_pTimeslot->m_unCCADuration = DCDiagGetInt32 ("Enter CCS Duration (0-65535): ", 0, 65535, p_pTimeslot->m_unCCADuration);
    p_pTimeslot->m_unTxOffset = DCDiagGetInt32 ("Enter Tx Offset (0-65535): ", 0, 65535, p_pTimeslot->m_unTxOffset);
    p_pTimeslot->m_unRxOffset = DCDiagGetInt32 ("Enter Rx Offset (0-65535): ", 0, 65535, p_pTimeslot->m_unRxOffset);
    p_pTimeslot->m_unRxWait = DCDiagGetInt32 ("Enter Rx Wait (0-65535): ", 0, 65535, p_pTimeslot->m_unRxWait);
    p_pTimeslot->m_unRxAckDelay = DCDiagGetInt32 ("Enter Rx Ack Delay (0-65535): ", 0, 65535, p_pTimeslot->m_unRxAckDelay);
    p_pTimeslot->m_unRxAckWait = DCDiagGetInt32 ("Enter Ack Wait (0-65535): ", 0, 65535, p_pTimeslot->m_unRxAckWait);
    p_pTimeslot->m_unTxAckDelay = p_pTimeslot->m_unRxAckDelay + (p_pTimeslot->m_unRxAckWait/2);
       DiagPrintf("Enter Tx Ack Delay (auto): %d\n", p_pTimeslot->m_unTxAckDelay);
    p_pTimeslot->m_unRxToTx = DCDiagGetInt32 ("Enter Rx to Tx (0-65535): ", 0, 65535, p_pTimeslot->m_unRxToTx);
    p_pTimeslot->m_unMaxTxAckDuration = DCDiagGetInt32 ("Enter Maximim Tx Ack Duration (0-65535): ", 0, 65535, p_pTimeslot->m_unMaxTxAckDuration);

    tmp = Lget24(p_pTimeslot->m_unMaxTxDPDUDuration);
    tmp = DCDiagGetInt32("Enter Maximum Tx DPDU Duration (0-16777215): ", 0, 16777215, tmp);
    Lput24( p_pTimeslot->m_unMaxTxDPDUDuration, tmp );

    tmp = Lget24(p_pTimeslot->m_unTimeslotLen);
    tmp = DCDiagGetInt32("Enter Timeslot Len (0-16777215): ", 0, 16777215, tmp);
    Lput24( p_pTimeslot->m_unTimeslotLen, tmp );
}


#if !defined(CELL_SBS)
void rf_config4e( int deprecated )
{
    if (deprecated)
    {
       DiagPrintf("\n*** WARNING ***");
       DiagPrintf("\nThe settings in this menu are now deprecated, any changes will not be saved.");
       DiagPrintf("\nUse the 'CRC Adder' configuration option to set the Network Id.");
       DiagPrintf("\n");
    }

    PERSIST_Load (PERSIST_STRUCT_4E);
    if( c_aPersistData[PERSIST_STRUCT_4E].m_pDefaultDataStruct )
    {
        unsigned char answer;
        DiagPrintf("\n4E Configuration -- ");
        answer = 0xdf & DCDiagGetChar ("Load DEFAULTS? (Y/N): ", "YyNn");
        if (answer == 'Y')
        {
            memcpy( c_aPersistData[PERSIST_STRUCT_4E].m_pDataStruct, c_aPersistData[PERSIST_STRUCT_4E].m_pDefaultDataStruct, c_aPersistData[PERSIST_STRUCT_4E].m_ucDataStructLen );
        }
    }

    if (deprecated)
    {
        DiagPrintf("\nNet ID and Capabilities\n");
#ifndef JUPITER_MESH_SUPPORTED
        g_stPersist4e.m_unNetId = DCDiagGetInt32 ("Enter Net ID (0-65535): ", 0, 65535, g_stPersist4e.m_unNetId);
#endif
    }
    else
    {
#if defined(REGION_JAPAN) || defined(JUPITER_MESH_SUPPORTED)
        DiagPrintf("\nNet ID and Capabilities\n");
        g_stPersist4e.m_unNetId = DCDiagGetInt32 ("Enter Net ID (0-65535): ", 0, 65535, g_stPersist4e.m_unNetId);
#else
        DiagPrintf("\nCapabilities\n");
#endif //REGION_JAPAN || JUPITER_MESH_SUPPORTED
    }
    g_stPersist4e.m_unCapabilities = DCDiagGetInt32 ("Enter Capabilities (0-65535): ", 0, 65535, g_stPersist4e.m_unCapabilities);

    DiagPrintf("\nMIBS\n");
    g_stPersist4e.m_ucCrtSecurityLevel = DCDiagGetInt32 ("Enter Current Security Level (0-7): ", 0, 7, g_stPersist4e.m_ucCrtSecurityLevel);
    g_stPersist4e.m_ucMinSecurityLevel = DCDiagGetInt32 ("Enter Minimum Security Level (0-7): ", 0, 7, g_stPersist4e.m_ucMinSecurityLevel);
    g_stPersist4e.m_ucMaxFrameRetries = DCDiagGetInt32 ("Enter Maximum Frame Retries (0-255): ", 0, 255, g_stPersist4e.m_ucMaxFrameRetries);
    g_stPersist4e.m_ucBattLifeExtPeriods = DCDiagGetInt32 ("Enter Battery Life Ext Periods (0-255): ", 0, 255, g_stPersist4e.m_ucBattLifeExtPeriods);
    g_stPersist4e.m_ucSimpleAddress = DCDiagGetInt32 ("Enter Simple Address (0-255): ", 0, 255, g_stPersist4e.m_ucSimpleAddress);
    g_stPersist4e.m_ucMaxCSMABackoffs = DCDiagGetInt32 ("Enter Maximum CSMA Backoffs (0-255): ", 0, 255, g_stPersist4e.m_ucMaxCSMABackoffs);

    DiagPrintf("\nMIBS Definitions\n");
    g_stPersist4e.m_unDisconnectTime = DCDiagGetInt32 ("Enter Disconnect Time (0-65535): ", 0, 65535, g_stPersist4e.m_unDisconnectTime);
    g_stPersist4e.m_unDiscSFLen = DCDiagGetInt32 ("Enter Discovery SF Len (0-65535): ", 0, 65535, g_stPersist4e.m_unDiscSFLen);
    g_stPersist4e.m_ucMinBE = DCDiagGetInt32 ("Enter Minimum BE (0-31): ", 0, 31, g_stPersist4e.m_ucMinBE);
    g_stPersist4e.m_ucMaxBE = DCDiagGetInt32 ("Enter Maximum BE (0-31): ", 0, 31, g_stPersist4e.m_ucMaxBE);

    DiagPrintf("\nRF mode\nEnter Mode idx (0-FF): ");
    g_stPersist4e.m_ucModeIdx = (unsigned char) DCDiagGetHexString( 2, g_stPersist4e.m_ucModeIdx );
    g_stPersist4e.m_ucReserved1 = 0xFF;

    DiagPrintf("\nDiscovery Timeslot\n");
    getPersistTimeslot( &g_stPersist4e.m_stDiscTimeslot );

    DiagPrintf("\nData Timeslot\n");
    getPersistTimeslot( &g_stPersist4e.m_stDataTimeslot );

#if defined(REGION_JAPAN)
    DiagPrintf ("\nEnter Data Channel Bitmap\n");
    getPersistBitmap( g_stPersist4e.m_aucChannelMask, 0 );
    getPersistBitmap( &g_stPersist4e.m_aucChannelMask[MAX_CHANNEL_MASK_LEN/2], 1);
#endif //REGION_JAPAN

    if (!deprecated)
    {
        PERSIST_Save (PERSIST_STRUCT_4E);
    }
}
#endif // !defined(CELL_SBS)

#ifdef WISUN_FAN_SUPPORT
void rf_configWiSun( void )
{
   uint16_t i;

   /******Note on accepting Broadcast Schedule Information for PAN Coordinator*******/
   /* We can chose to enter separate Channel Function, Custom Channel List &
     * Excluded Channel Information for a PAN coordinator, which is independent of the
     * corresponding entries for Unicast.
     * All the Non-PAN coordinator nodes receive this information from their 'Broadcast Master'
     * neighbor in the PC frame, and adopt it as there own.
     * Note that basic information - channel plan/ regulatory domain/ channel spacing/
     * rf baudrate/ Ch0 frequency would remain the same for unicast & broadcast.
     */

   DiagPrintf("\r\n");
   DiagPrintf("Following configurations can be set from WiSUN Menu:\r\n");
   DiagPrintf("   Channel plan     [0]: Controlled by Regulatory Domain\r\n");
   DiagPrintf("                    [1]: Controlled by user defined custom channels\r\n");
   DiagPrintf("   Channel spacing  [0]: 200 KHz\r\n");
   DiagPrintf("                    [1]: 400 KHz\r\n");
   DiagPrintf("                    [2]: 600 KHz\r\n");
   DiagPrintf("                    [3]: 100 KHz\r\n");
   DiagPrintf("   Channel Function [0]: Fixed Channel\r\n");
   DiagPrintf("                    [1]: TR51CF- Not supported\r\n");
   DiagPrintf("                    [2]: DH1CF\r\n");
   DiagPrintf("                    [3]: Vendor specific- Not supported\r\n");
   DiagPrintf("   Excluded Channel Control   [0]: No Excluded Channel\r\n");
   DiagPrintf("                              [1]: Excluded Channel Ranges\r\n");
   DiagPrintf("                              [2]: Excluded Channel Mask\r\n");
   DiagPrintf("   Operating Mode   [1]: 50kbps  (Modulation Index: a=0.5; b=1.0)\r\n");
   DiagPrintf("                    [2]: 100kbps (Modulation Index: a=0.5; b=1.0)\r\n");
   DiagPrintf("                    [3]: 150kbps\r\n");
   DiagPrintf("                    [4]: 200kbps (Modulation Index: a=0.5; b=1.0)\r\n");
   DiagPrintf("   Unicast Dwell interval : 15ms <= UDI <= 250 ms\r\n");

#ifdef TRANSCEIVER_SUPPORT
   DiagPrintf("   Broadcast Dwell interval : 15ms <= BDI <= 250 ms\r\n");
   DiagPrintf("   Broadcast Schedule Identifier : 0 <= BSI <= 65535\r\n");
   DiagPrintf("   Broadcast interval : 2xUDI <= BI <= 999999 ms\r\n");
#endif

   DiagPrintf("   Routing Method   [0]: L2 Mesh- Not supported\r\n");
   DiagPrintf("                    [1]: RPL\r\n");
   DiagPrintf("   Async Trickle Timer Imin  : 1 s <= Imin <= 60 s (default value = 60)\r\n");
   DiagPrintf("   Async Trickle Timer Imax  : 1 <= Imax <= 4 (default = 4, Imin*(2^Imax)=max trickle)\r\n");
   DiagPrintf("   Async Trickle Timer K     : 0 <= K <= 10 (default value = 1)\r\n");
   DiagPrintf("   Unicast DFE ACK  [0]: Unicast DFE without ACK\r\n");
   DiagPrintf("                    [1]: Unicast DFE with ACK - Default\r\n");
   DiagPrintf("   Unicast retries     : 0 <=  Retries <= 10 \r\n");
   DiagPrintf("   Broadcast retries   : 0 <=  Retries <= 10 \r\n");
   DiagPrintf("   CCA on/off in transmissions  [0]: CCA Off\r\n");
   DiagPrintf("                                [1]: CCA On - Default\r\n");
   DiagPrintf("   Backoff exponent    : 0 <=  BE <= 31 \r\n");
   DiagPrintf("   Clock Drift      [0 - 255]: HW clock drift in PPM (255: Not specified)\r\n");
   DiagPrintf("   Capabilities     [0]: To use FW defined default values\r\n");
   DiagPrintf("                    [1 - 65535]: To use values defined in diag mode\r\n\r\n");

   PERSIST_Load (PERSIST_STRUCT_4E);

   //Capabilities
   g_stPersist4e.m_unCapabilities = DCDiagGetInt32 ("Enter Capabilities (0-65535 (0 - to load Default FW values)): ", 0, 65535, g_stPersist4e.m_unCapabilities);
   if( g_stPersist4e.m_unCapabilities == 0 )
   {
      DiagPrintf("WiSUN default values will be loaded \n");
      PERSIST_Save( PERSIST_STRUCT_4E );
      return;
   }

   //Get the Channel Plan (Regulatory or Custom)
   g_stPersist4e.m_ucChannelPlan =
      DCDiagGetInt32("Enter Channel Plan (0-1): ", 0, 1, g_stPersist4e.m_ucChannelPlan);
   switch( g_stPersist4e.m_ucChannelPlan )
   {
      case CHANNEL_PLAN_REG_DOMAIN:
      {
         g_stPersist4e.m_ucRegulatoryDomain = RamChanMask_M = (unsigned int)DCDiagGetInt32("Enter Regulatory Domain (0-255): ", 0, 255, g_stPersist4e.m_ucRegulatoryDomain);
         switch( RamChanMask_M )
         {
            case REG_CODE_NORTH_AMERICA:
               DiagPrintf("Regulatory Domain = NORTH AMERICA\r\n");
               break;
            case REG_CODE_JAPAN:
               DiagPrintf("Regulatory Domain = JAPAN\r\n");
               break;
            case REG_CODE_INDIA:
               DiagPrintf("Regulatory Domain = INDIA\r\n");
               break;
            case REG_CODE_BRAZIL:
               DiagPrintf("Regulatory Domain = Brazil\r\n");
               break;
            default:
               DiagPrintf("Regulatory Domain %d not supported, defaulting to 1\r\n", RamChanMask_M);
               g_stPersist4e.m_ucRegulatoryDomain = RamChanMask_M = REG_CODE_NORTH_AMERICA;
               break;
         }

         //Get the operating mode & sub mode
         g_stPersist4e.m_u4OpMode = DCDiagGetInt32 ("Enter PHY Operating Mode (1-4): ", 1, 4, g_stPersist4e.m_u4OpMode);
         switch( g_stPersist4e.m_u4OpMode )
         {
            case 1: // 50 kbps
            case 2: //100 kbps
            case 4: //200 kbps
            {
               g_stPersist4e.m_u4OpModeSub =
                  DCDiagGetInt32 ("Enter sub-mode (0=a; 1=b): ", 0, 1, g_stPersist4e.m_u4OpModeSub);
               break;
            }
            default: // 150 and 300 kbps have no a/b option
               g_stPersist4e.m_u4OpModeSub = 0;
               break;
         }

         break;
      }
      case CHANNEL_PLAN_CUSTOM:
      {
         g_stPersist4e.m_ucRegulatoryDomain = RamChanMask_M = REG_CODE_CUSTOM;

         //Get the operating mode & sub mode
         g_stPersist4e.m_u4OpMode = DCDiagGetInt32 ("Enter PHY Operating Mode (1-4): ", 1, 4, g_stPersist4e.m_u4OpMode);
         switch( g_stPersist4e.m_u4OpMode )
         {
            case 1: // 50 kbps
            case 2: //100 kbps
            case 4: //200 kbps
            {
               g_stPersist4e.m_u4OpModeSub =
                  DCDiagGetInt32 ("Enter sub-mode (0=a; 1=b): ", 0, 1, g_stPersist4e.m_u4OpModeSub);
               break;
            }
            default: // 150 and 300 kbps have no a/b option
               g_stPersist4e.m_u4OpModeSub = 0;
               break;
         }

         g_stPersist4e.m_unChan0kHz = DCDiagGetInt32 ("Enter Ch 0 Frequency in kHz  (863100-928000): ", 863100, 927900, g_stPersist4e.m_unChan0kHz/*902200*/);
         g_stPersist4e.m_ucChanSpacingEnum = DCDiagGetInt32 ("Enter Channel Spacing Enum (0-3): ", 0, 3, g_stPersist4e.m_ucChanSpacingEnum/*0*/);
         g_stPersist4e.m_usUcastNumChannels = DCDiagGetInt32 ("Enter Number of Unicast Channels in band (1-129): ", 1, 129, g_stPersist4e.m_usUcastNumChannels/*129*/);
#ifdef TRANSCEIVER_SUPPORT
         g_stPersist4e.m_usBcastNumChannels = DCDiagGetInt32 ("Enter Number of Broadcast Channels in band (1-129): ", 1, 129, g_stPersist4e.m_usBcastNumChannels/*129*/);
#endif
         break;
      }
      default:
      {
         DiagPrintf("Unsupported Channel Plan %d\r\n", g_stPersist4e.m_ucChannelPlan);
         g_stPersist4e.m_ucChannelPlan = 0;
      }
   }

   //Initialize the stuffs
   uint8_t err = RegCode_Init( &g_stPersist4e );
   if( err & 1 )
   {
      DiagPrintf("Unknown Regulatory Domain\r\n");
      return;
   }
   if( err & 2 )
   {
      DiagPrintf("Invalid custom configuration\r\n");
      return;
   }
   if( err & 4 )
   {
      DiagPrintf("Unsupported PHY Op Mode\r\n");
      return;
   }
   eeUpdateImage();

   //Get number of channels supported for selected PHY mode (this number includes excluded channels)
   g_stPersist4e.m_usUcastNumChannels = RegCode_GetMaxChannels();
#ifdef TRANSCEIVER_SUPPORT
   if( g_stPersist4e.m_ucChannelPlan == CHANNEL_PLAN_REG_DOMAIN )
   {
      /*In PAN coordinator, we are setting up the broadcast schedule independently at this moment.
       *Since the regulatory domain configuration remains same for unicast and broadcast,
       * we take the maximum channels available for the selected config (in regulatory domain)
       *as the number of channels available for broadcast as well*/

      g_stPersist4e.m_usBcastNumChannels = RegCode_GetMaxChannels();
   }
   else if( g_stPersist4e.m_ucChannelPlan == CHANNEL_PLAN_CUSTOM )
   {
      /*For custom channel plan, this number is separately entered by the
      *user and has been saved into currentPhySettings at time of RegCode_Init*/
       g_stPersist4e.m_usBcastNumChannels = RegCode_GetMaxBcastChannels();
   }
#endif

   //Get the Channel Function (Fixed or DH1CF)
   g_stPersist4e.m_ucUcastChannelFunc = DCDiagGetInt32 ("Enter Unicast Channel Function (0-3): ", 0, 3, g_stPersist4e.m_ucUcastChannelFunc);
   switch( g_stPersist4e.m_ucUcastChannelFunc )
   {
      case CHANNEL_FUNCTION_FIXED:
      {
         memset( g_stPersist4e.m_aucChannelMask, 0xFF, MAX_CHANNEL_MASK_LEN ); /*exclude all channels by default*/

         if( RegCode_GetId() == REG_CODE_BRAZIL )
         {
            const ExcludedChanRange_t* range;

            //If regulatory domain is BRAZIL , then user should not enter the fixed channel from the reg domain excluded list
            range = RegDomainWisun_GetExclChannelRange();
            DiagPrintf("\nChannel no. %d to %d are excluded in Brazil regulatory domain. Do not enter these\n", range->m_usStartChan, range->m_usEndChan );

            do{
                 /*Prompt the user for a valid fix channel number. Reprompt if it lies in the excluded (by regulatory) range*/
                 g_stPersist4e.m_ucUcastFixChannelNum = i = DCDiagGetInt32 ("Enter Channel number:", 0, (g_stPersist4e.m_usUcastNumChannels - 1), g_stPersist4e.m_ucUcastFixChannelNum);
              }while( ( g_stPersist4e.m_ucUcastFixChannelNum >= range->m_usStartChan ) && ( g_stPersist4e.m_ucUcastFixChannelNum <= range->m_usEndChan ) );
         }
         else
         {
            /*simply accept a channel number between 0 and total number of channels*/
            g_stPersist4e.m_ucUcastFixChannelNum = i = DCDiagGetInt32 ("Enter Channel number:", 0, (g_stPersist4e.m_usUcastNumChannels - 1), g_stPersist4e.m_ucUcastFixChannelNum);
         }
         g_stPersist4e.m_aucChannelMask[i/8] &= ~(1 << i%8); /*enabled the fixed channel*/
         break;
      }
      case CHANNEL_FUNCTION_DH1CF:
      {
         DiagPrintf("DH1CF\r\n");
         break;
      }
      default:
      {
         DiagPrintf("Unicast Channel Function %d not supported, defaulting to DH1CF\r\n", g_stPersist4e.m_ucUcastChannelFunc);
         g_stPersist4e.m_ucUcastChannelFunc = 2;
      }
   }

   //Get Unicast Excluded channels
   if( g_stPersist4e.m_ucUcastChannelFunc == CHANNEL_FUNCTION_FIXED )
   {
      g_stPersist4e.m_ucUcastExChannelControl = NO_EXCLUDED_CHANNEL_INFO;
   }
   else
   {
      uint8_t startRangeIndex = 0;
      const ExcludedChanRange_t* range;

      //If the regulatory domain already includes an excluded channel range, the user must choose
      //excluded channel ranges.
      range = RegDomainWisun_GetExclChannelRange();
      if( range->m_usStartChan != 0xFFFF )
      {
         startRangeIndex = 1;
         g_stPersist4e.m_ucUcastExChannelControl = EXCLUDED_CHANNEL_RANGES;
         g_stPersist4e.m_stUcastExclRanges.m_usStartCh[0] = range->m_usStartChan;
         g_stPersist4e.m_stUcastExclRanges.m_usEndCh[0] = range->m_usEndChan;
      }
      else
      {
         g_stPersist4e.m_ucUcastExChannelControl =
            DCDiagGetInt32 ("Enter Unicast Excluded Channel Control (0-2): ", 0, 2,
                               g_stPersist4e.m_ucUcastExChannelControl);
      }

      switch( g_stPersist4e.m_ucUcastExChannelControl )
      {
         case NO_EXCLUDED_CHANNEL_INFO:
         {
            memset( g_stPersist4e.m_aucChannelMask, 0, MAX_CHANNEL_MASK_LEN );
            DiagPrintf("No Excluded Channels\r\n");
            break;
         }
         case EXCLUDED_CHANNEL_RANGES:
         {
            //Clear the excluded channel bitmask
            memset( g_stPersist4e.m_aucChannelMask, 0, MAX_CHANNEL_MASK_LEN );

            //Get the number of excluded channel ranges from the user
            snprintf( (char*)s_aTmpDiagBuffer, sizeof(s_aTmpDiagBuffer),
                       "Number of unicast excluded channel ranges (%d-%d): ",
                          startRangeIndex, MAX_EXCLUDED_CHAN_RANGES);
            g_stPersist4e.m_stUcastExclRanges.m_ucNumRanges = DCDiagGetInt32((char*)s_aTmpDiagBuffer,
                                                      startRangeIndex, MAX_EXCLUDED_CHAN_RANGES,
                                                      g_stPersist4e.m_stUcastExclRanges.m_ucNumRanges);

            for( i = startRangeIndex; i < g_stPersist4e.m_stUcastExclRanges.m_ucNumRanges; i++ )
            {
               //Get the start channel for range 'i'
               snprintf( (char*)s_aTmpDiagBuffer, sizeof(s_aTmpDiagBuffer),
                          "Start Channel for Excluded Range %d (0-%d): ", i,
                             ( g_stPersist4e.m_usUcastNumChannels - 1) );
               g_stPersist4e.m_stUcastExclRanges.m_usStartCh[i] = DCDiagGetInt32((char*)s_aTmpDiagBuffer,
                     0, (g_stPersist4e.m_usUcastNumChannels - 1), g_stPersist4e.m_stUcastExclRanges.m_usStartCh[i]);

               //Get the end channel for range 'i'
               snprintf( (char*)s_aTmpDiagBuffer, sizeof(s_aTmpDiagBuffer),
                          "End Channel for Excluded Range %d (%d-%d): ", i,
                          g_stPersist4e.m_stUcastExclRanges.m_usStartCh[i], ( g_stPersist4e.m_usUcastNumChannels - 1 ) );
               g_stPersist4e.m_stUcastExclRanges.m_usEndCh[i] = DCDiagGetInt32((char*)s_aTmpDiagBuffer,
                                                   g_stPersist4e.m_stUcastExclRanges.m_usStartCh[i],
                                                   ( g_stPersist4e.m_usUcastNumChannels - 1 ),
                                                   g_stPersist4e.m_stUcastExclRanges.m_usEndCh[i]);
            }

            break;
         }
         case EXCLUDED_CHANNEL_MASK:
         {
            for( i=0; i<((g_stPersist4e.m_usUcastNumChannels+7)/8); i++ )
            {
               DiagPrintf("Enter excluded channels bitmap [%d .. %d]: ", (8*i)+7, 8*i);
               g_stPersist4e.m_aucChannelMask[i] =
                  DCDiagGetHexString( 2, g_stPersist4e.m_aucChannelMask[i] );
            }
            break;
         }
         default:
         {
            DiagPrintf("Unsupported Excluded Channel Control %d", g_stPersist4e.m_ucUcastExChannelControl);
            g_stPersist4e.m_ucUcastExChannelControl = NO_EXCLUDED_CHANNEL_INFO;
         }
      }
   }
   DiagPrintf("\r\n");

   //Unicast Dwell Interval (a.k.a., slot time)
   g_stPersist4e.m_ucUDI = DCDiagGetInt32 ("Enter Unicast Dwell interval (15-250 ms): ", 15, 250, g_stPersist4e.m_ucUDI);

   //Routing Method (RPL)
   g_stPersist4e.m_ucRoutingMethod = DCDiagGetInt32 ("Enter Routing Method (1-1): ", 1, 1, g_stPersist4e.m_ucRoutingMethod);

#ifdef TRANSCEIVER_SUPPORT /*Non PAN coordinator nodes copy info from the received BS-IE*/
   /******Note on accepting Broadcast Schedule Information for PAN Coordinator*******/
   /* We can chose to enter separate Channel Function, Custom Channel List &
     * Excluded Channel Information for a PAN coordinator, which is independent of the
     * corresponding entries for Unicast.
     * All the Non-PAN coordinator nodes receive this information from their 'Broadcast Master'
     * neighbor in the PC frame, and adopt it as there own.
     * Note that basic information - channel plan/ regulatory domain/ channel spacing/
     * rf baudrate/ Ch0 frequency would remain the same for unicast & broadcast.
     */


   //Get the Broadcast Channel Function (Fixed or DH1CF)
   g_stPersist4e.m_ucBcastChannelFunc = DCDiagGetInt32 ("Enter Broadcast Channel Function (0-3): ", 0, 3, g_stPersist4e.m_ucBcastChannelFunc);
   switch( g_stPersist4e.m_ucBcastChannelFunc )
   {
      case CHANNEL_FUNCTION_FIXED:
      {
         memset( g_stPersist4e.m_aucBcastChannelMask, 0xFF, MAX_CHANNEL_MASK_LEN ); /*exclude all channels by default*/

         if( RegCode_GetId() == REG_CODE_BRAZIL )
         {
            const ExcludedChanRange_t* range;

            //If the regulatory domain already includes an excluded channel range, the user must choose
            //excluded channel ranges.
            range = RegDomainWisun_GetExclChannelRange();
            DiagPrintf("\nChannel no. %d to %d are excluded in Brazil regulatory domain. Do not enter these\n", range->m_usStartChan, range->m_usEndChan );

            do{
                 /*Prompt the user for a valid fix channel number. Reprompt if it lies in the excluded (by regulatory) range*/
                 g_stPersist4e.m_ucBcastFixChannelNum = i = DCDiagGetInt32 ("Enter Channel number:", 0, (g_stPersist4e.m_usBcastNumChannels - 1), g_stPersist4e.m_ucBcastFixChannelNum);
              }while( ( g_stPersist4e.m_ucBcastFixChannelNum >= range->m_usStartChan ) && ( g_stPersist4e.m_ucBcastFixChannelNum <= range->m_usEndChan ) );
         }
         else
         {
            /*simply accept a channel number between 0 and total number of channels*/
            g_stPersist4e.m_ucBcastFixChannelNum = i = DCDiagGetInt32 ("Enter Channel number:", 0, (g_stPersist4e.m_usBcastNumChannels - 1), g_stPersist4e.m_ucBcastFixChannelNum);
         }
         g_stPersist4e.m_aucBcastChannelMask[i/8] &= ~(1 << ( i % 8 ) ); /*enabled the fixed channel*/
         break;
      }
      case CHANNEL_FUNCTION_DH1CF:
      {
         DiagPrintf("DH1CF\r\n");
         break;
      }
      default:
      {
         DiagPrintf("Broadcast Channel Function %d not supported, defaulting to DH1CF\r\n", g_stPersist4e.m_ucBcastChannelFunc);
         g_stPersist4e.m_ucBcastChannelFunc = CHANNEL_FUNCTION_DH1CF;
      }
   }

   //Get Broadcast Excluded channels
   if( g_stPersist4e.m_ucBcastChannelFunc == CHANNEL_FUNCTION_FIXED )
   {
      g_stPersist4e.m_ucBcastExChannelControl = NO_EXCLUDED_CHANNEL_INFO;
   }
   else
   {
      uint8_t startRangeIndex = 0;
      const ExcludedChanRange_t* range;

      //If the regulatory domain already includes an excluded channel range, the user must choose
      //excluded channel ranges.
      range = RegDomainWisun_GetExclChannelRange();
      if( range->m_usStartChan != 0xFFFF )
      {
         startRangeIndex = 1;
         g_stPersist4e.m_ucBcastExChannelControl = EXCLUDED_CHANNEL_RANGES;
         g_stPersist4e.m_stBcastExclRanges.m_usStartCh[0] = range->m_usStartChan;
         g_stPersist4e.m_stBcastExclRanges.m_usEndCh[0] = range->m_usEndChan;
      }
      else
      {
         g_stPersist4e.m_ucBcastExChannelControl =
            DCDiagGetInt32 ("Enter Broadcast Excluded Channel Control (0-2): ", 0, 2,
                               g_stPersist4e.m_ucBcastExChannelControl);
      }

      switch( g_stPersist4e.m_ucBcastExChannelControl )
      {
         case NO_EXCLUDED_CHANNEL_INFO:
         {
            memset( g_stPersist4e.m_aucBcastChannelMask, 0, MAX_CHANNEL_MASK_LEN );
            DiagPrintf("Bcast: No Excluded Channels\r\n");
            break;
         }
         case EXCLUDED_CHANNEL_RANGES:
         {
            //Clear the excluded channel bitmask
            memset( g_stPersist4e.m_aucBcastChannelMask, 0, MAX_CHANNEL_MASK_LEN );

            //Get the number of excluded channel ranges from the user
            snprintf( (char*)s_aTmpDiagBuffer, sizeof(s_aTmpDiagBuffer),
                       "Number of broadcast excluded channel ranges (%d-%d): ",
                          startRangeIndex, MAX_EXCLUDED_CHAN_RANGES);
            g_stPersist4e.m_stBcastExclRanges.m_ucNumRanges = DCDiagGetInt32((char*)s_aTmpDiagBuffer,
                                                      startRangeIndex, MAX_EXCLUDED_CHAN_RANGES,
                                                      g_stPersist4e.m_stBcastExclRanges.m_ucNumRanges);

            for( i = startRangeIndex; i<g_stPersist4e.m_stBcastExclRanges.m_ucNumRanges; i++ )
            {
               //Get the start channel for range 'i'
               snprintf( (char*)s_aTmpDiagBuffer, sizeof(s_aTmpDiagBuffer),
                          "Bcast: Start Channel for Excluded Range %d (0-%d): ", i,
                             ( g_stPersist4e.m_usBcastNumChannels - 1) );
               g_stPersist4e.m_stBcastExclRanges.m_usStartCh[i] = DCDiagGetInt32((char*)s_aTmpDiagBuffer,
                     0, (g_stPersist4e.m_usBcastNumChannels - 1), g_stPersist4e.m_stBcastExclRanges.m_usStartCh[i]);

               //Get the end channel for range 'i'
               snprintf( (char*)s_aTmpDiagBuffer, sizeof(s_aTmpDiagBuffer),
                          "Bcast: End Channel for Excluded Range %d (%d-%d): ", i,
                          g_stPersist4e.m_stBcastExclRanges.m_usStartCh[i], ( g_stPersist4e.m_usBcastNumChannels - 1 ) );
               g_stPersist4e.m_stBcastExclRanges.m_usEndCh[i] = DCDiagGetInt32((char*)s_aTmpDiagBuffer,
                                                   g_stPersist4e.m_stBcastExclRanges.m_usStartCh[i],
                                                   ( g_stPersist4e.m_usBcastNumChannels - 1 ),
                                                   g_stPersist4e.m_stBcastExclRanges.m_usEndCh[i]);
            }

            break;
         }
         case EXCLUDED_CHANNEL_MASK:
         {
            for( i=0; i<((g_stPersist4e.m_usBcastNumChannels+7)/8); i++ )
            {
               DiagPrintf("Enter broadcast excluded channels bitmap [%d .. %d]: ", (8*i)+7, 8*i);
               g_stPersist4e.m_aucBcastChannelMask[i] =
                  DCDiagGetHexString( 2, g_stPersist4e.m_aucBcastChannelMask[i] );
            }
            break;
         }
         default:
         {
            DiagPrintf("Unsupported Broadcast Excluded Channel Control %d", g_stPersist4e.m_ucBcastExChannelControl);
            g_stPersist4e.m_ucBcastExChannelControl = NO_EXCLUDED_CHANNEL_INFO;
         }
      }
   }
   DiagPrintf("\r\n");

   g_stPersist4e.m_ucBDI = DCDiagGetInt32 ("Enter BDI (15-250 ms): ", 15, 250, g_stPersist4e.m_ucBDI);
   g_stPersist4e.m_usBSI = DCDiagGetInt32 ("Enter BSI (0-65535): ", 0, 65535, g_stPersist4e.m_usBSI);
   /*Limiting BI value to approx 999 seconds as more than 6 digits not allowed to enter in current diag implementation*/
   DiagPrintf("Enter Broadcast Interval (ms) (%d-999999)", ( 2 * g_stPersist4e.m_ucBDI ) ); /*BI/BDI atleast 2*/
   g_stPersist4e.m_unBI = DCDiagGetInt32 ("[Recommended value 4xBDI]: ", ( 2 * g_stPersist4e.m_ucBDI ), 999999, g_stPersist4e.m_unBI);

   /*Max value of PAN ID is limited to 0xFFFE since 0xFFFF is used to indicate 'PAN ID NOT PRESENT' in received messages*/
   g_stPersist4e.m_unPANId = DCDiagGetInt32 ("Enter PAN ID (0-65534): ", 0, 65534, g_stPersist4e.m_unPANId);
#endif

   //Async Trickle timer default values Imin = 60000 (60sec)  Imax = 4  //60 * (2^Imax) = 960 sec   K = 1
   g_stPersist4e.m_unDiscImin = DCDiagGetInt32 ("Enter Async trickle timer Imin (1000ms-60000ms): ", 1000, 60000, g_stPersist4e.m_unDiscImin);
   g_stPersist4e.m_ucDiscImax = DCDiagGetInt32 ("Enter Async trickle timer Imax (1-4): ", 1, 4, g_stPersist4e.m_ucDiscImax);
   g_stPersist4e.m_ucDiscK = DCDiagGetInt32 ("Enter Async trickle timer redundancy constant K (0-10): ", 0, 10, g_stPersist4e.m_ucDiscK);

   //Unicast with ACK enabled or not  Default is 1 (Unicast with ACK)
   g_stPersist4e.m_ucUnicastACK = DCDiagGetInt32 ("Enter 1 if ACK is required with DFE transmission  (0-1): ", 0, 1, g_stPersist4e.m_ucUnicastACK);

   //Unicast frame retries
   g_stPersist4e.m_ucUcastFrameRetries = DCDiagGetInt32 ("Enter Unicast frame retries (0-10): ", 0, 10, g_stPersist4e.m_ucUcastFrameRetries);
   //Broadcast frame retries
   g_stPersist4e.m_ucBcastFrameRetries = DCDiagGetInt32 ("Enter Broadcast frame retries (0-10): ", 0, 10, g_stPersist4e.m_ucBcastFrameRetries);
   //Unicast Backoff exponent
   g_stPersist4e.m_ucUcastMinBE = DCDiagGetInt32 ("Enter Minimum BE for Unicast(0-31): ", 0, 31, g_stPersist4e.m_ucUcastMinBE);
   g_stPersist4e.m_ucUcastMaxBE = DCDiagGetInt32 ("Enter Maximum BE for Unicast(0-31): ", 0, 31, g_stPersist4e.m_ucUcastMaxBE);
   //Broadcast Backoff exponent
   g_stPersist4e.m_ucBcastMinBE = DCDiagGetInt32 ("Enter Minimum BE for Broadcast(0-31): ", 0, 31, g_stPersist4e.m_ucBcastMinBE);
   g_stPersist4e.m_ucBcastMaxBE = DCDiagGetInt32 ("Enter Maximum BE for Broadcast(0-31): ", 0, 31, g_stPersist4e.m_ucBcastMaxBE);

   //CCA on / off for ASYNC  / Unicast  / Broadcast
   uint8_t temp;
   temp = g_stPersist4e.m_ucCcaFlags;
   g_stPersist4e.m_ucCcaFlags = DCDiagGetInt32 ("Enter 0 if CCA is required to turn off in Unicast transmissions(0-1): ", 0, 1, temp & BIT0);
   g_stPersist4e.m_ucCcaFlags |= ( DCDiagGetInt32 ("Enter 0 if CCA is required to turn off in Broadcast transmissions (0-1): ", 0, 1, ( temp & BIT1 ) >> 1) ) << 1;
   g_stPersist4e.m_ucCcaFlags |= ( DCDiagGetInt32 ("Enter 0 if CCA is required to turn off in Async transmissions (0-1): ", 0, 1, ( temp & BIT2 ) >> 2) ) << 2;

   // HW Clock Drift in PPM
   g_stPersist4e.m_ucClockDrift = DCDiagGetInt32("Enter clock drift(ppm) (0-255): ", 0, 255, g_stPersist4e.m_ucClockDrift);

   PERSIST_Save( PERSIST_STRUCT_4E );

   rf_displayChannelMask();
   DiagPrintf( "Symbol rate: %d kbps\r\n", RegCode_GetDataRateKbps(0));

}
#endif //WISUN_FAN_SUPPORT

void getPersistBitmap( uint8_t * p_pBitmap, uint8_t temp )
{
  DiagPrintf ("Enter channels bitmap %s", temp ? "[64 .. 95]: " : "[ 0 .. 31]: ");
  p_pBitmap = put32( p_pBitmap, DCDiagGetHexString( 8, get32( p_pBitmap ) ) );

  DiagPrintf ("Enter channels bitmap %s", temp ? "[ 96 .. 127]: " : "[ 32 ..  63]: ");
  put32( p_pBitmap, DCDiagGetHexString( 8, get32( p_pBitmap ) ) );
}



void rf_displayRfBaudrate( void )
{
    DiagPrintf( "RF baudrate: %d kbps", RegCode_GetDataRateKbps( s_stSelectedTestMode.stOpMode.bStartMode ) );
    if( s_unAckMode )
    {
        DiagPrintf(" ACK mode");
    }
    if( s_stSelectedTestMode.stOpMode.bStartMode != s_stSelectedTestMode.stOpMode.bNextMode )
    {
        DiagPrintf( " switch mode to %d kbps\n", RegCode_GetDataRateKbps( s_stSelectedTestMode.stOpMode.bNextMode ) );
    }
    else
    {
        DiagPrintf(" WITHOUT switch mode\n");
    }

}

uint8_t rf_checkChannelNumber( uint16_t channelNumber )
{
   uint16_t maxChNo = RegCode_GetMaxChannels();

   if( channelNumber > maxChNo )
   {
      return FALSE;
   }

#if defined(WISUN_FAN_SUPPORT)

   uint8_t i;

   //Check if the channel is excluded by the excluded channel bitmask
   if( g_stPersist4e.m_ucUcastExChannelControl == EXCLUDED_CHANNEL_MASK &&
       g_stPersist4e.m_aucChannelMask[channelNumber/8] & ( 1 << (channelNumber%8) ) )
   {
      return FALSE; //Channel is excluded
   }

   //Check if the channel is excluded by the excluded channel range(s)
   if( g_stPersist4e.m_ucUcastExChannelControl == EXCLUDED_CHANNEL_RANGES )
   {
      for( i=0; i<g_stPersist4e.m_stUcastExclRanges.m_ucNumRanges; i++ )
      {
         if( channelNumber >= g_stPersist4e.m_stUcastExclRanges.m_usStartCh[i] &&
             channelNumber <= g_stPersist4e.m_stUcastExclRanges.m_usEndCh[i] )
         {
            return FALSE; //Channel is excluded
         }
      }
   }

   //If channel is not excluded, return TRUE
   return TRUE;

#else

   const uint8_t* chMask = RegionGetChannelMask();
   return ( chMask[channelNumber/8] & (1<<(7-(channelNumber%8))) );

#endif //WISUN_FAN_SUPPORT
}

void rf_displayChannel( uint32_t chNo )
{
    uint32_t freqkHz;

    freqkHz = RegCode_GetFrequencyKHz(chNo);
    DiagPrintf("\nNow using RF Channel: %u [ %u.%03u Mhz ]\n", chNo, freqkHz / 1000, freqkHz % 1000);
}

void rf_displayChannelMask()
{
    uint16_t i,maxChNo;
    uint32_t freqkHz;

    maxChNo = RegCode_GetMaxChannels();

#if defined(WISUN_FAN_SUPPORT)

   uint8_t j;

   //Display the channel information and data rate
   for( i=0; i<maxChNo; i++ )
   {
      //Check if the channel is excluded by the excluded channel bitmask
      if( ( g_stPersist4e.m_ucUcastExChannelControl == EXCLUDED_CHANNEL_MASK ) &&
          g_stPersist4e.m_aucChannelMask[i/8] & (1<<i%8) )
      {
         continue; //Channel is excluded
      }

      //Check if the channel is excluded by the excluded channel range(s)
      if( g_stPersist4e.m_ucUcastExChannelControl == EXCLUDED_CHANNEL_RANGES )
      {
         for( j=0; j<g_stPersist4e.m_stUcastExclRanges.m_ucNumRanges; j++ )
         {
            if( i >= g_stPersist4e.m_stUcastExclRanges.m_usStartCh[j] &&
                i <= g_stPersist4e.m_stUcastExclRanges.m_usEndCh[j] )
            {
               break; //Channel is excluded
            }
         }
         if( j < g_stPersist4e.m_stUcastExclRanges.m_ucNumRanges )
         {
            continue;
         }
      }

      if( ( g_stPersist4e.m_ucUcastExChannelControl == NO_EXCLUDED_CHANNEL_INFO ) &&
         g_stPersist4e.m_aucChannelMask[i/8] & ( 1 << ( i % 8 ) ) )
      {
         continue; //Channel is excluded
      }

      freqkHz = RegCode_GetFrequencyKHz(i);
      DiagPrintf("\t%d\t(%u.%03u MHz)\n", i, freqkHz / 1000, freqkHz % 1000);
   }

#ifdef TRANSCEIVER_SUPPORT
   /*In a PAN coordinator, separate channels have been enabled/excluded for broadcast*/

   if( g_stPersist4e.m_ucChannelPlan == CHANNEL_PLAN_REG_DOMAIN )
   {
      /*In PAN coordinator, we are setting up the broadcast schedule independently at this moment.
          *Since the regulatory domain configuration remains same for unicast and broadcast,
          * we take the maximum channels available for the selected config (in regulatory domain)
          *as the number of channels available for broadcast as well*/

      maxChNo = RegCode_GetMaxChannels();
   }
   else if( g_stPersist4e.m_ucChannelPlan == CHANNEL_PLAN_CUSTOM )
   {
      /*For custom channel plan, this number is separately entered by the
          *user and has been saved into currentPhySettings at time of RegCode_Init*/
      maxChNo = RegCode_GetMaxBcastChannels();
   }
   DiagPrintf("\nBroadcast Channel List:\n");

   /*Display the channel information*/
   for( i = 0; i < maxChNo; i++ )
   {
      //Check if the channel is excluded by the excluded channel bitmask
      if( ( g_stPersist4e.m_ucBcastExChannelControl == EXCLUDED_CHANNEL_MASK ) &&
          g_stPersist4e.m_aucBcastChannelMask[i/8] & (1<<i%8) )
      {
         continue; //Channel is excluded
      }

      //Check if the channel is excluded by the excluded channel range(s)
      if( g_stPersist4e.m_ucBcastExChannelControl == EXCLUDED_CHANNEL_RANGES )
      {
         for( j = 0; j < g_stPersist4e.m_stBcastExclRanges.m_ucNumRanges; j++ )
         {
            if( i >= g_stPersist4e.m_stBcastExclRanges.m_usStartCh[j] &&
                i <= g_stPersist4e.m_stBcastExclRanges.m_usEndCh[j] )
            {
               break; //Channel is excluded
            }
         }
         if( j < g_stPersist4e.m_stBcastExclRanges.m_ucNumRanges )
         {
            continue;
         }
      }

      if( ( g_stPersist4e.m_ucBcastExChannelControl == NO_EXCLUDED_CHANNEL_INFO ) &&
         g_stPersist4e.m_aucBcastChannelMask[i/8] & ( 1 << ( i % 8 ) ) )
      {
         continue; //Channel is excluded
      }

      freqkHz = RegCode_GetFrequencyKHz(i);
      DiagPrintf("\t%d\t(%u.%03u MHz)\n", i, freqkHz / 1000, freqkHz % 1000);
   }
#endif /*TRANSCEIVER_SUPPORT*/

#else //WISUN_FAN_SUPPORT

    DiagPrintf("ChannelMask: \n");

    const uint8_t* chMask = RegionGetChannelMask();

    for( i=0; i<=maxChNo; i++ )
    {
        if( chMask[i/8] & (1<<(7-(i%8))) )
        {
            freqkHz = RegCode_GetFrequencyKHz(i);
            DiagPrintf("\t%d\t(%u.%03u MHz)\n", i, freqkHz / 1000, freqkHz % 1000);
        }
    }
    DiagPrintf("\n");

#endif //WISUN_FAN_SUPPORT
}

/*
   rf_square_wave -- Command Q

   This function will continuously output a single character. Outputing
   a 55h will cause a square wave to be generated.

   To output a square wave, we will cause the Tx interrupt routine to
   go into an infinite loop.
*/
void rf_square_wave (void)
{
    unsigned int unCrtTxCh = 0xFFFFFFFF;
    unsigned int cmd;

    DiagPrintf("Enter character to output (hex, (enter FE for random): ");
    rfTestCharacter = (unsigned char) DCDiagGetHexString(2, rfTestCharacter);

    memset( s_aTmpDiagBuffer, rfTestCharacter, sizeof(s_aTmpDiagBuffer) );

    while( (cmd = diagRFRuntimeCommands('Q')) != ESCAPE_CHAR )
    {
        // Change channel if user hit '+' or '-'
        if ((unCrtTxCh != g_unLastChannel) || (cmd == RESET_TEST))
        {
            unCrtTxCh = g_unLastChannel;
            L4G_SendPacket( L4G_GetCurrentRfMode( s_stSelectedTestMode.stOpMode.bStartMode
                                                , s_stSelectedTestMode.stOpMode.bStartMode
                                                , 0, 0, 0
                                                , RamMaxPower_M )
                          , HalTimerGetValueUs(TIMER_L4G) + 100  //Must be short to work with Initate Stream.  Use 100 to cover L4G response time.
                          , s_aTmpDiagBuffer
                          , 1024 );
            L4G_RF_InitiateStreamTransmit(rfTestCharacter);
        }

        if( rfTestCharacter == 0xFE )
        {
            for(unsigned int i = 0; i < 64/4; i++ )
            {
                ((uint32_t*)s_aTmpDiagBuffer)[i] = SYS_GetRandom32();
            }
        }
        else
        {
            for(unsigned int i = 0; i < 64/4; i++ )
            {
                ((uint32_t*)s_aTmpDiagBuffer)[i] = (rfTestCharacter << 24) + (rfTestCharacter << 16) + (rfTestCharacter << 8) + rfTestCharacter;
            }
        }

        L4G_ReloadTxContinous(s_aTmpDiagBuffer, 64);
        Pend( 0, 1 ); // to feed the watchdog
    }
    // Stop sending out the character
    L4G_RF_RetireStreamTransmit(L4G_GetCurrentRfMode(s_stSelectedTestMode.stOpMode.bStartMode, 0, 0, 0, 0, RamMaxPower_M));
    L4G_AbortCrtAction();
}


/* Command M Receive Ping Packets */
#pragma optimize=none
void rf_receive_ping(unsigned char p_ucType)
{
   unsigned int unCrtTxCh = 0xFFFFFFFF;
   OS_TASK_EVENT ucCrtEvent;
   unsigned long usRxCounter = 0;

   L4G_GetLastRxMsg(); // discard RX pending message
   OS_ClearEvents( &TCB_L4E_Task ); // clear all pending events

   CurrentRfMode_t stRxMode = s_stSelectedTestMode;
   if(p_ucType == RECEIVE_ACK)
   {
      stRxMode.stOpMode.bStartMode = s_stSelectedTestMode.stOpMode.bNextMode;
      stRxMode.stOpMode.bNextMode = s_stSelectedTestMode.stOpMode.bNextMode;
   }
   else
   {
       rfTestRxPackets = 0;
#ifdef SERIES5
       if(p_ucType == RECEIVE_PING)
       {
          for (int i=0;i<5;i++) L4G_RF_MeasureNoise();  //Insure SX1233 BG Noise has smoothed
       }
#endif
   }


   while( 1 )
   {
      if (unCrtTxCh != g_unLastChannel)
      {
         unCrtTxCh = g_unLastChannel;
         L4G_StartRx( stRxMode, 0, 0 );
      }

      ucCrtEvent = OS_WaitEventTimed( 0xFF, 98 );
      if( ucCrtEvent & L4E_EVENT_RX_PHR )
      {
         if( rfTestVerbose == VB_PACKET )
         {
            DiagPrintf("\tRX PHR: FEI %d,LNA %d\n", (signed int)((signed short)g_unLastFEI) * 5722 / 100, (int8_t)g_stRxMsg.m_ucLNA );  //SX1233 LNA is 0 to 6 range step.  CC1200 LNA is +63 to -64 in dB.
         }
         usRxCounter = 0;
         ucCrtEvent = OS_WaitEventTimed( L4E_EVENT_RX_MSG | L4E_EVENT_RX_BAD_MSG, g_stTSTemplate.m_unMaxWaitEndMsg_Ms );
      }
      if( ucCrtEvent & L4E_EVENT_RX_BAD_MSG )  //Bad Message has higher priority then Message Done
      {
          L4G_StartRx( stRxMode, 0, 0 ); // restart RX
      }
      if( ucCrtEvent & L4E_EVENT_RX_MSG )
      {
          const L4G_RX_MSG *  pstRxMsg = L4G_GetLastRxMsg();
          if( pstRxMsg )
          {
              const uint8_t * pRxPk = pstRxMsg->m_ucData+2;

              if(p_ucType == RECEIVE_ACK)
              {
                  if( !memcmp( pRxPk, c_aAck, 3+MAC_ADDR_LEN+MAC_ADDR_LEN ) )
                  {
                      DiagPrintf( "RX ack\n" );
                      rfTestRxPackets++;
                  }
                  else
                  {
                      DiagPrintf( "unexpected packet received ...\n" );
                  }
                  break;
              }
              else // wait for ping
              {
                  unsigned int unDataOffset = 3+MAC_ADDR_LEN+MAC_ADDR_LEN;
                  if(    !memcmp( pRxPk, c_aMHRPingPlain, 2 )
                      && !memcmp( pRxPk+3, g_oEUI64, MAC_ADDR_LEN )
                      && !memcmp( pRxPk+unDataOffset, c_aMHRPingPlain+unDataOffset, sizeof(c_aMHRPingPlain) - unDataOffset ) )
                  {
                      // packet to me
                      rfTestRxPackets++;

                      DiagPrintf( "RX packet no %d from %08X%08X\n"
                                  , pRxPk[2]
                                  , Lget32(pRxPk+3+MAC_ADDR_LEN+4)
                                  , Lget32(pRxPk+3+MAC_ADDR_LEN));

                      c_aAck[2] = pRxPk[2];
                      memcpy( c_aAck+3, pRxPk+3+MAC_ADDR_LEN, MAC_ADDR_LEN );
                      memcpy( c_aAck+3+MAC_ADDR_LEN, g_oEUI64, MAC_ADDR_LEN );

                      CurrentRfMode_t stAckIdx;
                      stAckIdx.ucAllFlags = 0;
                      if( pstRxMsg->m_ucModeSwitch & SWITCH_MODE_EXISTS ) // switch mode
                      {
                          stAckIdx.stOpMode.bStartMode = pstRxMsg->m_ucModeSwitch & L4G_MODE_IDX_MASK;
                          stAckIdx.stOpMode.bNextMode = pstRxMsg->m_ucModeSwitch & L4G_MODE_IDX_MASK;
                      }
                      else // not switch mode
                      {
                          stAckIdx.stOpMode.bStartMode = s_stSelectedTestMode.stOpMode.bStartMode & L4G_MODE_IDX_MASK;
                          stAckIdx.stOpMode.bNextMode = s_stSelectedTestMode.stOpMode.bStartMode & L4G_MODE_IDX_MASK;
                      }

                      if (rfTestVerbose == VB_PACKET)
                      {
                         printHexData( c_aAck, sizeof(c_aAck) );
                      }
                      L4G_SendPacket( stAckIdx, HalTimerGetValueUs(TIMER_L4G) + TX_DELAY_PERIOD, c_aAck, sizeof(c_aAck));

                      if( !(L4E_EVENT_TX_COMPLETE & OS_WaitEventTimed( L4E_EVENT_TX_COMPLETE, 425 )) )
                      {
                          DiagPrintf("TX ERROR timeout\n" );
                      }
                      else if( g_ucTxCallbackCode != SFC_SUCCESS )
                      {
                          DiagPrintf("TX ERROR %d\n", g_ucTxCallbackCode );
                      }
                  }
              }
          }
          L4G_StartRx( stRxMode, 0, 0 ); // restart RX
          usRxCounter = 0;
      }

        else if (p_ucType == RECEIVE_ACK)
        {
            if( (++usRxCounter) >= 2 ) // 200 ms
            {
                DiagPrintf( "no ack received ...\n" );
                break;
            }
        }
        else
        {
            if( (++usRxCounter) >= 9 ) // 1000 ms
            {
                L4G_AbortCrtAction();  //Also clear modem flags
                L4G_StartRx( stRxMode, 0, 0 ); // restart RX
                usRxCounter = 0;
            }
        }
      if( diagRFRuntimeCommands ('R') == ESCAPE_CHAR )  // wait until escape char
      {
          break;
      }
   }
   L4G_AbortCrtAction();
}

#pragma optimize=none
void rf_ping_to(void)
{
  uint64_t u64DestAddress;
  DiagPrintf ("Enter 64-bit dest addr: ");
  u64DestAddress = get_hex_longlong();  // Note that DCDiagGetHexString only returns 32 bits.

  memcpy(c_aMHRPingPlain + 3, (unsigned char*)&u64DestAddress, MAC_ADDR_LEN);
  memcpy(c_aMHRPingPlain+3+MAC_ADDR_LEN, g_oEUI64, MAC_ADDR_LEN);

  memcpy(c_aAck + 3, g_oEUI64, MAC_ADDR_LEN);
  memcpy(c_aAck+3+MAC_ADDR_LEN, (unsigned char*)&u64DestAddress, MAC_ADDR_LEN);

  int i;

   if( rfTestPacketLength > L4G_MAX_PK_SIZE )
   {
      rfTestPacketLength = L4G_MAX_PK_SIZE;
   }

   memcpy(s_aTmpDiagBuffer, c_aMHRPingPlain, sizeof(c_aMHRPingPlain));
   i = sizeof(c_aMHRPingPlain);

   if(rfTestDataType == 'C')
   {
      for(; i < rfTestPacketLength; i++)
         s_aTmpDiagBuffer[i] = rfTestData;
   }
   else if(rfTestDataType == 'S')
   {
      int j;
      for(j = 0; i < rfTestPacketLength; i++)
      {
         s_aTmpDiagBuffer[i] = rfTestString[j++];
         if(j > rfTestStrlen)
            j = 0;
      }
   }
   else
   {
      for(; i < rfTestPacketLength; i++)
         s_aTmpDiagBuffer [i] = rand ();
   }

#ifdef SERIES5
         for (int i=0;i<5;i++) L4G_RF_MeasureNoise();  //Insure SX1233 BG Noise has smoothed
#endif


      Pend (0, 20);           // Give other guy time to get ready
      HRFPortCrcError = 0;
      rfTestRxPackets = 0;

   rfTestTxSent = 0;
   while ((++rfTestTxSent <= rfTestPacketNumber) || !rfTestPacketNumber)
   {
      s_aTmpDiagBuffer[2] = c_aAck[2] = c_aMHRPingPlain[2] = rfTestTxSent & 0xFF;
      if( rfTestVerbose != VB_OFF )
      {
           DiagPrintf("TX pk %d, size %d -> ", rfTestTxSent, rfTestPacketLength);
      }
      if (rfTestVerbose == VB_PACKET)
      {
          printHexData( s_aTmpDiagBuffer, rfTestPacketLength );
      }

      L4G_SendPacket( s_stSelectedTestMode, HalTimerGetValueUs(TIMER_L4G) + TX_DELAY_PERIOD, s_aTmpDiagBuffer, rfTestPacketLength );

      if( !(L4E_EVENT_TX_COMPLETE & OS_WaitEventTimed( L4E_EVENT_TX_COMPLETE, 425 )) )
      {
          DiagPrintf("TX ERROR timeout\n" );
      }
      else if( g_ucTxCallbackCode != SFC_SUCCESS )
      {
          DiagPrintf("TX ERROR %d\n", g_ucTxCallbackCode );
      }
      else
      {
          rf_receive_ping (RECEIVE_ACK);
      }

      if (diagRFRuntimeCommands ('T') == ESCAPE_CHAR)
         {
         ++rfTestTxSent;       // Fake out the printout
         break;
         }

      Pend (0, rfTestTxDelay);
      }

   DiagPrintf ("Packets Sent %u, Packets Rx %u\n", --rfTestTxSent, rfTestRxPackets);
}

#if !defined(CELL_SBS)
void setDefaultRFparam(void)
{
    rfTestDataType = 'R';
    rfTestDataAck = 'N';
    rfTestPacketLength = 512;
    rfTestPacketNumber = 0xFFFFFFFF;
    HRFPortCrcError = 0;
    rfTestData = 0x55;
    rfTestTxDelay = 300;
    rfTestCharacter = 0x55;
    rfTestLockDelay = 20;

    // Make sure board is initialized and channel set
    L4E_MIB_Init();
    g_ucAssociationStatus = L4E_DEVICE_ASSOCIATED;
    L4G_RF_Init();
    L4G_AbortCrtAction();

    L4G_RF_SetChannel( g_stHopSeq.m_aunSequence[0] );
    s_stSelectedTestMode.stOpMode.bStartMode = L4G_FSK_OPMODE_1;
    s_stSelectedTestMode.stOpMode.bNextMode = 0;
    s_stSelectedTestMode.ucAllFlags = 0;
    s_unAckMode = 0;
    rftest_check4eHeader = 1;
}


#ifdef S5_EV_LCE
void
diagTestmodeExecute	(void)
{
    char TestModeState = 1;
    while (1)
    {
      switch (TestModeState)
      {
        case 1:
        {
            Pend(0,250);
            DiagPrintf ("Executing Test Mode 1...\nTx Freq 902.2 MHz continuous, modulated, random data, 50 Kbps \n");
            rfTestDataAck = 'N';
            s_stSelectedTestMode.stOpMode.bStartMode = L4G_FSK_OPMODE_1;
            s_stSelectedTestMode.stOpMode.bNextMode = 0;
            s_stSelectedTestMode.ucAllFlags = 0;
            L4G_RF_SetChannel(0); // Set channel to 902.2 MHz
            RF_LED_SET;
            do
                {
                    diagSendTest(FALSE);
                }while (BUTTON_STATE);
            RF_LED_CLR;
            TestModeState++;
            break;
        }
        case 2:
        {
            Pend(0,250);
            DiagPrintf ("Executing Test Mode 2...\nTx Freq 915 MHz continuous, modulated, random data, 50 Kbps \n");
            L4G_RF_SetChannel(64);
            RF_LED_SET;
            do
                {
                    diagSendTest(FALSE);
                }while (BUTTON_STATE);
            RF_LED_CLR;
            TestModeState++;
            break;
        }
        case 3:
        {
            Pend(0,250);
            DiagPrintf ("Executing Test Mode 3...\nTx Freq 927.6 MHz continuous, modulated, random data, 50 Kbps \n");
            L4G_RF_SetChannel(127);
            RF_LED_SET;
            do
                {
                    diagSendTest(FALSE);
                }while (BUTTON_STATE);
            RF_LED_CLR;
            TestModeState++;
            break;
        }
        case 4:
        {
            Pend(0,250);
            DiagPrintf ("Executing Test Mode 4...\nRx loopback mode Freq 902.2 MHz, 200 Kbps \n");
            rfTestDataAck = 'A';
            L4G_RF_SetChannel(0);
            s_stSelectedTestMode.stOpMode.bStartMode = L4G_FSK_OPMODE_3;
            s_stSelectedTestMode.stOpMode.bNextMode = 0;
            s_stSelectedTestMode.ucAllFlags = 0;
            do
                {
                    diagReceiveTest(0);
                }while (BUTTON_STATE);
            TestModeState++;
            break;
        }
        case 5:
        {
            Pend(0,250);
            DiagPrintf ("Executing Test Mode 5...\nRx loopback mode Freq 915 MHz, 200 Kbps \n");
            L4G_RF_SetChannel(64);
            do
                {
                    diagReceiveTest(0);
                }while (BUTTON_STATE);
            TestModeState++;
            break;
        }
        case 6:
        {
            Pend(0,250);
            DiagPrintf ("Executing Test Mode 6...\nRx loopback mode Freq 927.6 MHz, 200 Kbps \n");
            L4G_RF_SetChannel(127);
            do
                {
                    diagReceiveTest(0);
                }while (BUTTON_STATE);
            TestModeState = 1;
            break;
        }

        default:
            TestModeState = 1;
            break;

      }
    }
}
#endif //if S5_EV_LCE
#endif // !defined(CELL_SBS)

#ifdef MAC_PHY_SNIFF_SUPPORT
void sniffParseMacPhy( const L4G_RX_MSG *  pRxMsg, unsigned int targetPANID, unsigned int targetRouteBPANID, unsigned char frameDisplayFilter, unsigned char routeDisplayFilter )
{
  static const char * c_aFrameType[8] = { "BCN","DAT","ACK","CMD","?LL","?MP","?06","?07" };
  unsigned int bIsRouteB = ( *( pRxMsg->m_ucData + 0 ) & L4G_HDR_FCS16BITS );

  if(( targetPANID == Lget16( pRxMsg->m_ucData+5 ) || ( targetPANID == DISPLAY_ALL_PANIDS ) || ( targetRouteBPANID == Lget16( pRxMsg->m_ucData+5 ))) && // always accept local route B PANID
    (((( *( pRxMsg->m_ucData+2 ) &  MHR_FRAME_TYPE_MASK ) == MHR_FRAME_TYPE_BEACON )  && (( frameDisplayFilter ) == DISPLAY_BEACON_FRAMES ))  || // beacon frame and should be displayed
    ( (( *( pRxMsg->m_ucData+2 ) &  MHR_FRAME_TYPE_MASK ) == MHR_FRAME_TYPE_DATA )    && (( frameDisplayFilter ) == DISPLAY_DATA_FRAMES ))    || // data frame and should be displayed
    ( (( *( pRxMsg->m_ucData+2 ) &  MHR_FRAME_TYPE_MASK ) == MHR_FRAME_TYPE_ACK )     && (( frameDisplayFilter ) == DISPLAY_ACK_FRAMES ))     || // ack frame and should be displayed
    ( (( *( pRxMsg->m_ucData+2 ) &  MHR_FRAME_TYPE_MASK ) == MHR_FRAME_TYPE_MAC_CMD ) && (( frameDisplayFilter ) == DISPLAY_COMMAND_FRAMES )) || // cmd frame and should be displayed
    ( (( *( pRxMsg->m_ucData+2 ) &  MHR_FRAME_TYPE_MASK ) != MHR_FRAME_TYPE_BEACON )  && (( frameDisplayFilter ) == IGNORE_BEACON_FRAMES ))   || // data or ack, beacons should be ignored
                                                   ((( frameDisplayFilter ) == DISPLAY_ALL_FRAMES )))  && // display all frame types
    ((( bIsRouteB == 0x00 ) && ( routeDisplayFilter  == DISPLAY_RTA )) ||         // only route A should be displayed
    (( bIsRouteB == 0x08 ) && ( routeDisplayFilter  == DISPLAY_RTB )) ||          // only route B should be displayed
                          (((( routeDisplayFilter ) == DISPLAY_RTA_AND_RTB ))))   // both route A and B should be displayed
    )
    {
      if( !ucCSVFormat )
      {
        DiagPrintf( "\nsniff(0x%08X) RT%c "
                 , SysTimeGet()
                 , (bIsRouteB ? 'B' : 'A')
              );
      }
      else
      {
        DiagPrintf( "\n%u,%c,"
              , SysTimeGet()
              , (bIsRouteB ? 'B' : 'A')
            );
      }

      if( !ucCSVFormat )
      {
        DiagPrintf( "PHR MS:%02X FCS:%02X DW:%02X len:%d\t" , ( *( pRxMsg->m_ucData+0 ) &  L4G_HDR_MODESWITCH )
                                                        , ( *( pRxMsg->m_ucData+0 ) &  L4G_HDR_FCS16BITS )
                                                        , ( *( pRxMsg->m_ucData+0 ) &  L4G_HDR_DATAWHITENING )
                                                        , pRxMsg->m_unRxDataLength
              );
      }
      else
      {
        DiagPrintf( "%02X,%02X,%02X,%d," , ( *( pRxMsg->m_ucData+0 ) &  L4G_HDR_MODESWITCH )
                                     , ( *( pRxMsg->m_ucData+0 ) &  L4G_HDR_FCS16BITS )
                                     , ( *( pRxMsg->m_ucData+0 ) &  L4G_HDR_DATAWHITENING )
                                     , pRxMsg->m_unRxDataLength
              );
      }

      if( !ucCSVFormat )
      {
        DiagPrintf( "MHR sq:%d \tRSSI:-%d dBm LQI:%d typ:%s SE:%c FP:%02X AR:%02X PANcmp:%02X SNsup:%c IE lst pres:%c d ad md:%02X ver:%02X s ad md:%02X "
                           , ( *( pRxMsg->m_ucData+4 )) // sequence number
                           , ( pstRxMsg->m_ucRSSI )
                           , L4G_RF_GetLQI( pRxMsg->m_ucRSSI, L4E_NEIGHBOR *p_pNeigh )
                           , c_aFrameType[*( pRxMsg->m_ucData+2 ) & 0x07]
                           , (( *( pRxMsg->m_ucData+2 ) & MHR_SECURITY_ENABLED_MASK ) ? 'y': 'n' )
                           , (( *( pRxMsg->m_ucData+2 ) >> 4 ) & 0x01 ) //Frame Pending
                           , (( *( pRxMsg->m_ucData+2 ) >> 5 ) & 0x01 ) //ACK Requested
                           , ( *( pRxMsg->m_ucData+2 ) >> 6 & 0x01 )    //PAN ID Compression
                           , (( *( pRxMsg->m_ucData+3 ) & 0x01 ) ? 'y': 'n' )   //Sequence Number Suppression
                           , ((( *( pRxMsg->m_ucData+3 ) >> 1 ) & 0x01 ) ? 'y': 'n' )   //Information Element List Present
                           , (( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) //Destination Address Mode
                           , (( *( pRxMsg->m_ucData+3 ) >> 4 ) & 0x03 ) //Version
                           , (( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) //Source Address Mode
            );
      }
      else
      {
        DiagPrintf( "%d,-%d,%d,%s,%c,%02X,%02X,%02X,%c,%c,%02X,%02X,%02X,"
                           , ( *( pRxMsg->m_ucData+4 )) // sequence number
                           , (pstRxMsg->m_ucRSSI )
                           , L4G_RF_GetLQI( pRxMsg->m_ucRSSI, L4E_NEIGHBOR *p_pNeigh )
                           , c_aFrameType[*( pRxMsg->m_ucData+2 ) & 0x07]
                           , (( *( pRxMsg->m_ucData+2 ) & MHR_SECURITY_ENABLED_MASK ) ? 'y': 'n' )
                           , (( *( pRxMsg->m_ucData+2 ) >> 4 ) & 0x01 )
                           , (( *( pRxMsg->m_ucData+2 ) >> 5 ) & 0x01 )
                           , ( *( pRxMsg->m_ucData+2 ) >> 6 & 0x01 )
                           , (( *( pRxMsg->m_ucData+3 ) & 0x01 ) ? 'y': 'n' )
                           , ((( *( pRxMsg->m_ucData+3 ) >> 1 ) & 0x01 ) ? 'y': 'n' )
                           , (( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 )
                           , (( *( pRxMsg->m_ucData+3 ) >> 4 ) & 0x03 )
                           , (( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 )
            );
      }


        if(( MHR_FRAME_TYPE_ACK == ( *( pRxMsg->m_ucData+2 ) & 0x07 )) && // frame type is ack
           (( *( pRxMsg->m_ucData+2 ) >> 6 & 0x01 ) == 1 ) // PANID compression is 1
          )
          {
            if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 3 )  && // src addr mode = 64 bit
                ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 3 )     // dst addr mode = 64 bit
                   )
            {
              if( !ucCSVFormat )
              {
                DiagPrintf( "s: %08X%08X, d: %08X%08X"  //Source address
                      , Lget32( pRxMsg->m_ucData+17 )
                      , Lget32( pRxMsg->m_ucData+13 )
                      , Lget32( pRxMsg->m_ucData+9 )
                      , Lget32( pRxMsg->m_ucData+5 )
                      );
              }
              else
              {
                DiagPrintf( "%08X%08X,%08X%08X"
                      , Lget32( pRxMsg->m_ucData+17 )
                      , Lget32( pRxMsg->m_ucData+13 )
                      , Lget32( pRxMsg->m_ucData+9 )
                      , Lget32( pRxMsg->m_ucData+5 )
                      );
              }
            }
            else if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 2 )  && // src addr mode = 16 bit
                     ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 2 )     // dst addr mode = 16 bit
                   )
            {
              if( !ucCSVFormat )
              {
                DiagPrintf( "s: %04X, d: %04X " //Destination Address
                    , Lget16( pRxMsg->m_ucData+9 )
                    , Lget16( pRxMsg->m_ucData+7 )
                    );
              }
              else
              {
                DiagPrintf( "%04X,%04X"
                    , Lget16( pRxMsg->m_ucData+9 )
                    , Lget16( pRxMsg->m_ucData+7 )
                    );
              }
            }
          }
        else if(( MHR_FRAME_TYPE_ACK == ( *( pRxMsg->m_ucData+2 ) & 0x07 )) && // frame type is ack
                (( *( pRxMsg->m_ucData+2 ) >> 6 & 0x01 ) == 0 ) // PANID compression is 0
               )
          {
            if(
              ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 2 )  &&  // src addr mode = 16 bit
              ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 3 )      // dest addr mode = 64 bit
              )
            {
              if( !ucCSVFormat )
              {
                DiagPrintf( "s: %04X, d: %08X%08X"
                      , Lget16( pRxMsg->m_ucData+15 )
                      , Lget32( pRxMsg->m_ucData+11 )
                      , Lget32( pRxMsg->m_ucData+7 )
                      );
              }
              else
              {
                DiagPrintf( "%04X,%08X%08X,"
                      , Lget16( pRxMsg->m_ucData+15 )
                      , Lget32( pRxMsg->m_ucData+11 )
                      , Lget32( pRxMsg->m_ucData+7 )
                      );
              }

              // print time correction IE
              if( !ucCSVFormat )
              {
                DiagPrintf( " TCIE: %d"
                         , Lget16( pRxMsg->m_ucData+19 )
                    );
              }
              else
              {
                DiagPrintf( ",%d"
                    , Lget16( pRxMsg->m_ucData+19 )
                    );
              }
            }
            else if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 3 )  && // src addr mode = 64 bit
                     ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 2 )     // dst addr mode = 16 bit
                   )
            {
              if( !ucCSVFormat )
              {
                DiagPrintf( "s: %08X%08X, d: %04X"
                      , Lget32( pRxMsg->m_ucData+13 )
                      , Lget32( pRxMsg->m_ucData+9 )
                      , Lget16( pRxMsg->m_ucData+7 )
                      );
              }
              else
              {
                DiagPrintf( "%08X%08X,%04X"
                        , Lget32( pRxMsg->m_ucData+13 )
                        , Lget32( pRxMsg->m_ucData+9 )
                        , Lget16( pRxMsg->m_ucData+7 )
                        );
              }
            }
            else if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 2 )  && // src addr mode = 16 bit
                     ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 2 )     // dst addr mode = 16 bit
                   )
            {
              if( !ucCSVFormat )
              {
                DiagPrintf( "s: %04X, d: %04X"
                    , Lget16( pRxMsg->m_ucData+9 )
                    , Lget16( pRxMsg->m_ucData+7 )
                      );
              }
              else
              {
                DiagPrintf( "%04X,%04X,"
                      , Lget16( pRxMsg->m_ucData+9 )
                      , Lget16( pRxMsg->m_ucData+7 )
                        );
              }

              // print time correction IE
              if( !ucCSVFormat )
              {
                DiagPrintf( " TCIE: %d"
                          , Lget16( pRxMsg->m_ucData+13 )
                    );
              }
              else
              {
                DiagPrintf( ",%d"
                      , Lget16( pRxMsg->m_ucData+13 )
                      );
              }

            }
        }
        else if(( 1 == ( *( pRxMsg->m_ucData+2 ) & 0x07 )) && // frame type is data
                (( *( pRxMsg->m_ucData+2 ) >> 6 & 0x01 ) == 1 ) // PANID compression is 1
                )
        {
          if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 2 )  && // src addr mode = 16 bit
              ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 2 )     // dst addr mode = 16 bit
              )
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %04X, d: %04X"
             , Lget16( pRxMsg->m_ucData+9 )
                    , Lget16( pRxMsg->m_ucData+7 )
                    );
            }
            else
            {
              DiagPrintf( "%04X,%04X"
                      , Lget16( pRxMsg->m_ucData+9 )
                      , Lget16( pRxMsg->m_ucData+7 )
                      );
            }
          }
          else if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 3 )  && // src addr mode = 64 bit
                   ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 3 )     // dst addr mode = 64 bit
              )
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %08X%08X, d: %08X%08X "
                       , Lget32( pRxMsg->m_ucData+17 )
                    , Lget32( pRxMsg->m_ucData+13 )
                    , Lget32( pRxMsg->m_ucData+9 )
                    , Lget32( pRxMsg->m_ucData+5 )
                    );
            }

            else
            {
              DiagPrintf( "%08X%08X,%08X%08X"
                      , Lget32( pRxMsg->m_ucData+17 )
                      , Lget32( pRxMsg->m_ucData+13 )
                      , Lget32( pRxMsg->m_ucData+9 )
                      , Lget32( pRxMsg->m_ucData+5 )
                      );
            }
          }
        }
        else if(( 1 == ( *( pRxMsg->m_ucData+2 ) & 0x07 )) && // frame type is data
                (( *( pRxMsg->m_ucData+2 ) >> 6 & 0x01 ) == 0 ) // PANID compression is 0
                )
        {
          if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 2 )  && // src addr mode = 16 bit
              ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 2 )     // dst addr mode = 16 bit
              )
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %04X, d: %04X"
                         , Lget16( pRxMsg->m_ucData+9 )
                    , Lget16( pRxMsg->m_ucData+7 )
                    );
            }
            else
            {
              DiagPrintf( "%04X,%04X"
                      , Lget16( pRxMsg->m_ucData+9 )
                      , Lget16( pRxMsg->m_ucData+7 )
                      );
            }
          }
          else if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 3 )  && // src addr mode = 64 bit
                   ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 2 )     // dst addr mode = 16 bit
              )
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %08X%08X, d: %04X"
                    , Lget32( pRxMsg->m_ucData+13 )
                    , Lget32( pRxMsg->m_ucData+9 )
                    , Lget16( pRxMsg->m_ucData+7 )
                    );
            }
            else
            {
              DiagPrintf( "%08X%08X,%04X"
                      , Lget32( pRxMsg->m_ucData+13 )
                      , Lget32( pRxMsg->m_ucData+9 )
                      , Lget16( pRxMsg->m_ucData+7 )
                      );
            }
          }
          else if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 3 )  && // src addr mode = 64 bit
                   ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 3 )     // dst addr mode = 64 bit
              )
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %08X%08X, d: %08X%08X"
                    , Lget32( pRxMsg->m_ucData+19 )
                    , Lget32( pRxMsg->m_ucData+15 )
                    , Lget32( pRxMsg->m_ucData+11 )
                    , Lget32( pRxMsg->m_ucData+7 )
                    );
            }
            else
            {
              DiagPrintf( "%08X%08X,%08X%08X"
                      , Lget32( pRxMsg->m_ucData+19 )
                      , Lget32( pRxMsg->m_ucData+15 )
                      , Lget32( pRxMsg->m_ucData+11 )
                      , Lget32( pRxMsg->m_ucData+7 )
                      );
            }
          }
          else
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %08X%08X"
                    , Lget32( pRxMsg->m_ucData+11 )
                    , Lget32( pRxMsg->m_ucData+7 )
                    );
            }
            else
            {
              DiagPrintf( "%08X%08X"
                      , Lget32( pRxMsg->m_ucData+11 )
                      , Lget32( pRxMsg->m_ucData+7 )
                      );
            }
          }
        }
        else if(( 3 == ( *( pRxMsg->m_ucData+2 ) & 0x07 )) && // frame type is cmd
                (( *( pRxMsg->m_ucData+2 ) >> 6 & 0x01 ) == 1 ) // PANID compression is 1
                )
        {
          if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 2 )  && // src addr mode = 16 bit
              ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 2 )     // dst addr mode = 16 bit
              )
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %04X, d: %04X"
                    , Lget16( pRxMsg->m_ucData+7 )
                    , Lget16( pRxMsg->m_ucData+9 )
                    );
            }
            else
            {
              DiagPrintf( "%04X,%04X"
                      , Lget16( pRxMsg->m_ucData+7 )
                      , Lget16( pRxMsg->m_ucData+9 )
                      );
            }
          }
          else
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %08X%08X, d: %08X%08X "
                      , Lget32( pRxMsg->m_ucData+9 )
                      , Lget32( pRxMsg->m_ucData+5 )
                      , Lget32( pRxMsg->m_ucData+17 )
                      , Lget32( pRxMsg->m_ucData+13 )
                      );
            }
            else
            {
              DiagPrintf( "%08X%08X,%08X%08X"
                      , Lget32( pRxMsg->m_ucData+9 )
                      , Lget32( pRxMsg->m_ucData+5 )
                      , Lget32( pRxMsg->m_ucData+17 )
                      , Lget32( pRxMsg->m_ucData+13 )
                      );
            }
          }
        }
        else if(( 3 == ( *( pRxMsg->m_ucData+2 ) & 0x07 )) && // frame type is cmd
                (( *( pRxMsg->m_ucData+2 ) >> 6 & 0x01 ) == 0 ) // PANID compression is 0
                )
        {
          if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 2 )  && // src addr mode = 16 bit
              ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 2 )     // dst addr mode = 16 bit
              )
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %04X, d: %04X"
                    , Lget16( pRxMsg->m_ucData+7 )
                    , Lget16( pRxMsg->m_ucData+9 )
                    );
            }
            else
            {
              DiagPrintf( "%04X,%04X"
                      , Lget16( pRxMsg->m_ucData+7 )
                      , Lget16( pRxMsg->m_ucData+9 )
                      );
            }
          }
          else if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 3 )  && // src addr mode = 64 bit
                   ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 2 )     // dst addr mode = 16 bit
              )
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %08X%08X, d: %04X"
                      , Lget32( pRxMsg->m_ucData+13 )
                      , Lget32( pRxMsg->m_ucData+9 )
                      , Lget16( pRxMsg->m_ucData+7 )
                      );
            }
            else
            {
              DiagPrintf( "%08X%08X,%04X"
                      , Lget32( pRxMsg->m_ucData+13 )
                      , Lget32( pRxMsg->m_ucData+9 )
                      , Lget16( pRxMsg->m_ucData+7 )
                      );
            }
          }
        }
        else if(( MHR_FRAME_TYPE_BEACON == ( *( pRxMsg->m_ucData+2 ) & 0x07 ))) // frame type is beacon
        {
          if( ((( *( pRxMsg->m_ucData+3 ) >> 6 ) & 0x03 ) == 3 )  && // src addr mode = 64 bit
              ((( *( pRxMsg->m_ucData+3 ) >> 2 ) & 0x03 ) == 3 )     // dst addr mode = 64 bit
                   )
            {
              if( !ucCSVFormat )
              {
                DiagPrintf( "s: %08X%08X, d: %08X%08X"
                      , Lget32( pRxMsg->m_ucData+19 )
                      , Lget32( pRxMsg->m_ucData+15 )
                      , Lget32( pRxMsg->m_ucData+11 )
                      , Lget32( pRxMsg->m_ucData+7 )
                    );
              }
              else
              {
                DiagPrintf( "%08X%08X,%08X%08X,"
                        , Lget32( pRxMsg->m_ucData+19 )
                        , Lget32( pRxMsg->m_ucData+15 )
                        , Lget32( pRxMsg->m_ucData+11 )
                        , Lget32( pRxMsg->m_ucData+7 )
                      );
              }
            }
          else
          {
            if( !ucCSVFormat )
            {
              DiagPrintf( "s: %08X%08X "
                      , Lget32( pRxMsg->m_ucData+11 )
                      , Lget32( pRxMsg->m_ucData+7 )
                    );
            }
            else
            {
              DiagPrintf( "%08X%08X,,"
                      , Lget32( pRxMsg->m_ucData+11 )
                      , Lget32( pRxMsg->m_ucData+7 )
                    );
            }
          }
          if( !ucCSVFormat )
          {
            DiagPrintf( "ASN: %02X%02X%02X%02X%02X"
                      , *( pRxMsg->m_ucData+29+3 )
                      , *( pRxMsg->m_ucData+28+3 )
                      , *( pRxMsg->m_ucData+27+3 )
                      , *( pRxMsg->m_ucData+26+3 )
                      , *( pRxMsg->m_ucData+25+3 )
                );
          }
          else
          {
            DiagPrintf( "%02X%02X%02X%02X%02X"
                      , *( pRxMsg->m_ucData+29+3 )
                      , *( pRxMsg->m_ucData+28+3 )
                      , *( pRxMsg->m_ucData+27+3 )
                      , *( pRxMsg->m_ucData+26+3 )
                      , *( pRxMsg->m_ucData+25+3 )
                );
          }
        }

        if( rfTestVerbose == VB_PACKET )
        {
          printHexData( pRxMsg->m_ucData, pRxMsg->m_unRxDataLength );
        }
  }
}
#endif // MAC_PHY_SNIFF_SUPPORT

#if !defined(CELL_SBS)
/*
** This is strictly a diagnostic only function. When in diagnostic mode the user can move the RF power up and down. Note that
** we are updating the RAM copy of the manufacturing constants. If the user does later save the manufacturing constants then
** any changes made here will stick. This should have no impact on manufacturing, so should not be an issue.
**
** For this device right now, RF power can range from 0 to 0x7F. The user can only change one power level setting at a time.
**
** For REXU there is a separate set of RF Power settings for each 2 MHz frequency range.  This routine only increments or
** decrements the Power Level for the current channel frequency range and Power Min/Med/Max range.
*/
unsigned int
HRFDiagChangePALevel (unsigned int increase)
{
#ifdef ELSTER_REX4
   unsigned char index = (g_unLastChannel / 10);  // convert RF channel to the 2MHz block of interest with 200 kHz channels
   unsigned char work;
   if      (RamMaxPower_M == 0) work = (fc_data.MfgTbl.RFPower21dBM[index]);
   else if (RamMaxPower_M == 1) work = (fc_data.MfgTbl.RFPower24dBM[index]);
   else if (RamMaxPower_M >= 2) work = (fc_data.MfgTbl.RFPower27dBM[index]);
   if (increase)
      {
      if (work < HRF_MAX_RF_POWER) work++;
      }
   else
      {
      if (work > HRF_MIN_RF_POWER) work--;
      }
   if      (RamMaxPower_M == 0) (fc_data.MfgTbl.RFPower21dBM[index] = work);
   else if (RamMaxPower_M == 1) (fc_data.MfgTbl.RFPower24dBM[index] = work);
   else if (RamMaxPower_M >= 2) (fc_data.MfgTbl.RFPower27dBM[index] = work);
   return work;

#else

   unsigned int work = HFCData.RFPower[RamMaxPower_M] & 0xFF;

   if (increase)
      {
      if (work < HRF_MAX_RF_POWER)
         work++;
      }
   else
      {
      if (work > HRF_MIN_RF_POWER)
         work--;
      }

   HFCData.RFPower[RamMaxPower_M] = (HFCData.RFPower[RamMaxPower_M] & ~0xFF) | work;

      /* Only return the actual PA value, don't return the dBm value */
   return (HFCData.RFPower[RamMaxPower_M] & 0xFF);
#endif
}
#endif // !defined(CELL_SBS)

#ifdef FWDL_UART_SUPPORT
unsigned char FWDL_UART_WaitForPacket(unsigned char *pkt_buf, FWDL_UART_Hdr * hdr)
{  
   unsigned int pkt_len = 0;
   
   pkt_len = FWDL_UART_GetBufferTimed(pkt_buf, FWDL_UART_MAX_LEN);
   
   if (pkt_buf[0] != FWDL_START_BYTE)
   {
      return FWDL_UART_BAD_START;
   }
   
   if (pkt_len < FWDL_UART_HDR_LEN)
   {
      return FWDL_UART_BAD_LEN;
   }
   
   hdr->flag = pkt_buf[1];
   hdr->offset = get32(&pkt_buf[2]);
   hdr->data_len = get16(&pkt_buf[6]);
   hdr->crc = get16(&pkt_buf[8]);
   
   if ((hdr->data_len + FWDL_UART_HDR_LEN) != pkt_len)
   {
      return FWDL_UART_BAD_LEN;
   }
   
   if ((uint16_t)crcBufferSlow(pkt_buf+FWDL_UART_HDR_LEN, hdr->data_len, 0xFFFF, CRC_RIGHT, CCIT_RIGHT) != hdr->crc)
   {
      return FWDL_UART_BAD_CRC;
   }
   
   return FWDL_UART_OK;
}


unsigned int FWDL_UART_GetBufferTimed(unsigned char *rx_buff, unsigned int data_len)
{
   unsigned int timeout = (unsigned int)SysTimeGet();
   unsigned int rx_len = 0; 
   unsigned int rx_idx = 0;
   
   //Wait for 3 second
   //max packet len is 10+256=266B, worst for baudrate 9600 is 0.277S      
   while ( (((unsigned int)SysTimeGet() - timeout) < 3000)
        && (rx_idx < data_len) )
   {
      rx_len = FWDL_UART_ReadBuff(rx_buff + rx_idx, data_len - rx_len);
      rx_idx +=  rx_len;
      
      Pend(0, 10);
   }

   return rx_idx;
}
#endif //FWDL_UART_SUPPORT

#endif /* DIAG_STRIPPED */
