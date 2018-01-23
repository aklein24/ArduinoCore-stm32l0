/*
 * Copyright (c) 2015-2018 Thomas Roell.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimers.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimers in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of Thomas Roell, nor the names of its contributors
 *     may be used to endorse or promote products derived from this Software
 *     without specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * WITH THE SOFTWARE.
 */

#include "armv6m.h"
#include "stm32l0xx.h"

#include "gnss_api.h"
#include "stm32l0_rtc.h"

/*
 * NOTES:
 *
 * SiRF/CSR
 *
 * $PSRFEPE,074155.799,A,1.3,10.59,52.97,0.6,180.0*16 
 *
 *     UTC
 *     Status       A/V (A == Valid, V = Invalid)
 *     HOP
 *     EHPE
 *     EVPE
 *     EHVE         Expected Horizontal Velocity Error
 *     EHE          Expected Heading Error
 *
 * Per NMEA definition GGA should be only GPGGA, while GNS can be GPGNS/GLGNS/GNGNS ...
 *
 * GPS        1-32
 * GLONASS    65-88
 * BEIDOU     121-157
 *
 */

/************************************************************************************/

#define NMEA_SENTENCE_MASK_GPGGA                      0x00000001
#define NMEA_SENTENCE_MASK_GPGSA                      0x00000002
#define NMEA_SENTENCE_MASK_GPGST                      0x00000004
#define NMEA_SENTENCE_MASK_GPGSV                      0x00000008
#define NMEA_SENTENCE_MASK_GPRMC                      0x00000010
#define NMEA_SENTENCE_MASK_GLGSA                      0x00000020
#define NMEA_SENTENCE_MASK_GLGSV                      0x00000040
#define NMEA_SENTENCE_MASK_SOLUTION                   0x00008000

#define NMEA_FIELD_SEQUENCE_START                     0
#define NMEA_FIELD_SEQUENCE_SKIP                      1
#define NMEA_FIELD_SEQUENCE_GGA_TIME                  2
#define NMEA_FIELD_SEQUENCE_GGA_LATITUDE              3
#define NMEA_FIELD_SEQUENCE_GGA_LATITUDE_NS           4
#define NMEA_FIELD_SEQUENCE_GGA_LONGITUDE             5
#define NMEA_FIELD_SEQUENCE_GGA_LONGITUDE_EW          6
#define NMEA_FIELD_SEQUENCE_GGA_QUALITY               7
#define NMEA_FIELD_SEQUENCE_GGA_NUMSV                 8
#define NMEA_FIELD_SEQUENCE_GGA_HDOP                  9
#define NMEA_FIELD_SEQUENCE_GGA_ALTITUDE              10
#define NMEA_FIELD_SEQUENCE_GGA_ALTITUDE_UNIT         11
#define NMEA_FIELD_SEQUENCE_GGA_SEPARATION            12
#define NMEA_FIELD_SEQUENCE_GGA_SEPARATION_UNIT       13
#define NMEA_FIELD_SEQUENCE_GGA_DIFFERENTIAL_AGE      14
#define NMEA_FIELD_SEQUENCE_GGA_DIFFERENTIAL_STATION  15
#define NMEA_FIELD_SEQUENCE_GSA_OPERATION             16
#define NMEA_FIELD_SEQUENCE_GSA_NAVIGATION            17
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_1         18
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_2         19
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_3         20
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_4         21
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_5         22
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_6         23
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_7         24
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_8         25
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_9         26
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_10        27
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_11        28
#define NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_12        29
#define NMEA_FIELD_SEQUENCE_GSA_PDOP                  30
#define NMEA_FIELD_SEQUENCE_GSA_HDOP                  31
#define NMEA_FIELD_SEQUENCE_GSA_VDOP                  32
#define NMEA_FIELD_SEQUENCE_GST_TIME                  33
#define NMEA_FIELD_SEQUENCE_GST_RANGE                 34
#define NMEA_FIELD_SEQUENCE_GST_STDDEV_MAJOR          35
#define NMEA_FIELD_SEQUENCE_GST_STDDEV_MINOR          36
#define NMEA_FIELD_SEQUENCE_GST_ORIENTATION           37
#define NMEA_FIELD_SEQUENCE_GST_STDDEV_LATITUDE       38
#define NMEA_FIELD_SEQUENCE_GST_STDDEV_LONGITUDE      39
#define NMEA_FIELD_SEQUENCE_GST_STDDEV_ALTITUDE       40
#define NMEA_FIELD_SEQUENCE_GSV_SENTENCES             41
#define NMEA_FIELD_SEQUENCE_GSV_CURRENT               42
#define NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_COUNT      43
#define NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_ID         44
#define NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_ELEV       45
#define NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_AZIM       46
#define NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_SNR        47
#define NMEA_FIELD_SEQUENCE_RMC_TIME                  48
#define NMEA_FIELD_SEQUENCE_RMC_STATUS                49
#define NMEA_FIELD_SEQUENCE_RMC_LATITUDE              50
#define NMEA_FIELD_SEQUENCE_RMC_LATITUDE_NS           51
#define NMEA_FIELD_SEQUENCE_RMC_LONGITUDE             52
#define NMEA_FIELD_SEQUENCE_RMC_LONGITUDE_EW          53
#define NMEA_FIELD_SEQUENCE_RMC_SPEED                 54
#define NMEA_FIELD_SEQUENCE_RMC_COURSE                55
#define NMEA_FIELD_SEQUENCE_RMC_DATE                  56
#define NMEA_FIELD_SEQUENCE_RMC_VARIATION             57
#define NMEA_FIELD_SEQUENCE_RMC_VARIATION_UNIT        58
#define NMEA_FIELD_SEQUENCE_RMC_MODE                  59
#define NMEA_FIELD_SEQUENCE_GGA_END                   60
#define NMEA_FIELD_SEQUENCE_GSA_END                   61
#define NMEA_FIELD_SEQUENCE_GST_END                   62
#define NMEA_FIELD_SEQUENCE_GSV_END                   63
#define NMEA_FIELD_SEQUENCE_RMC_END                   64
#define NMEA_FIELD_SEQUENCE_PMTK001_COMMAND           65
#define NMEA_FIELD_SEQUENCE_PMTK001_STATUS            66
#define NMEA_FIELD_SEQUENCE_PMTK001_END               67

#define NMEA_FIELD_MASK_TIME                          0x0001
#define NMEA_FIELD_MASK_POSITION                      0x0002
#define NMEA_FIELD_MASK_ALTITUDE                      0x0004
#define NMEA_FIELD_MASK_SPEED                         0x0008
#define NMEA_FIELD_MASK_COURSE                        0x0010
#define NMEA_FIELD_MASK_EHPE                          0x0020
#define NMEA_FIELD_MASK_EVPE                          0x0040
#define NMEA_FIELD_MASK_PDOP                          0x0080
#define NMEA_FIELD_MASK_HDOP                          0x0100
#define NMEA_FIELD_MASK_VDOP                          0x0200

#define NMEA_OPERATION_MANUAL                         0
#define NMEA_OPERATION_AUTOMATIC                      1

#define NMEA_NAVIGATION_NONE                          0
#define NMEA_NAVIGATION_2D                            1
#define NMEA_NAVIGATION_3D                            2

#define NMEA_STATUS_RECEIVER_WARNING                  0
#define NMEA_STATUS_DATA_VALID                        1

typedef struct _nmea_context_t {
    uint8_t             prefix;                             /* NMEA PREFIX (GP, GL, GN)     */
    uint8_t             sequence;                           /* FIELD SEQUENCE               */
    uint16_t            mask;                               /* FIELD MASK                   */
    uint8_t             navigation;                         /* GSA                          */
    uint8_t             status;                             /* RMC                          */
    uint8_t             sv_in_view_sentences;               /* GSV                          */
    uint8_t             sv_in_view_count;                   /* GSV                          */
    uint8_t             sv_in_view_index;                   /* GSV                          */
    uint8_t             sv_used_count;                      /* GSA                          */
    uint32_t            sv_used_mask[3];                    /* GSA                          */
    uint16_t            mtk_command;
    uint16_t            mtk_status;
} nmea_context_t;

/************************************************************************************/

#define UBX_MESSAGE_MASK_NAV_DOP                      0x00010000
#define UBX_MESSAGE_MASK_NAV_PVT                      0x00040000
#define UBX_MESSAGE_MASK_NAV_SVINFO                   0x00100000
#define UBX_MESSAGE_MASK_NAV_TIMEGPS                  0x00200000
#define UBX_MESSAGE_MASK_SOLUTION                     0x00008000

typedef struct _ubx_context_t {
    uint8_t             ck_a;
    uint8_t             ck_b;
    uint16_t            message;
    uint16_t            length;
    uint16_t            week;
    uint32_t            tow;
    uint32_t            itow;
    stm32l0_rtc_timer_t timeout;
} ubx_context_t;

/************************************************************************************/

#define GNSS_STATE_START              0
#define GNSS_STATE_NMEA_PAYLOAD       1
#define GNSS_STATE_NMEA_CHECKSUM_1    2
#define GNSS_STATE_NMEA_CHECKSUM_2    3
#define GNSS_STATE_NMEA_END_CR        4
#define GNSS_STATE_NMEA_END_LF        5
#define GNSS_STATE_UBX_SYNC_2         6
#define GNSS_STATE_UBX_MESSAGE_1      7
#define GNSS_STATE_UBX_MESSAGE_2      8
#define GNSS_STATE_UBX_LENGTH_1       9
#define GNSS_STATE_UBX_LENGTH_2       10
#define GNSS_STATE_UBX_PAYLOAD        11
#define GNSS_STATE_UBX_CK_A           12
#define GNSS_STATE_UBX_CK_B           13

#define GNSS_INIT_DONE                0
#define GNSS_INIT_MTK_BAUD_RATE       1
#define GNSS_INIT_MTK_INIT_TABLE      2
#define GNSS_INIT_UBX_BAUD_RATE       3
#define GNSS_INIT_UBX_INIT_TABLE      4

#define GNSS_RESPONSE_NONE            0
#define GNSS_RESPONSE_ACK             1
#define GNSS_RESPONSE_NACK            2
#define GNSS_RESPONSE_STARTUP         3
#define GNSS_RESPONSE_NMEA_SENTENCE   4
#define GNSS_RESPONSE_UBX_MESSAGE     5

#define GNSS_RX_DATA_SIZE             96
#define GNSS_TX_DATA_SIZE             64 /* UBX SET PERIODIC */
#define GNSS_TX_TABLE_COUNT           8  /* UBX SET PERIODIC */

typedef struct _gnss_device_t {
    uint8_t             ck_a;
    uint8_t             mode;
    uint8_t             state;
    volatile uint8_t    busy;
    volatile uint8_t    init;
    uint32_t            seen;
    uint32_t            expected;
    uint16_t            checksum;
    uint16_t            rx_count;
    uint16_t            rx_offset;
    uint16_t            rx_chunk;
    const uint8_t * const * volatile table;
    uint8_t             rx_data[GNSS_RX_DATA_SIZE];
    uint8_t             tx_data[GNSS_TX_DATA_SIZE];
    const uint8_t       *tx_table[GNSS_TX_TABLE_COUNT];
    nmea_context_t      nmea;
    ubx_context_t       ubx;
    gnss_location_t     location;
    gnss_satellites_t   satellites;
    volatile uint32_t   command;
    gnss_send_routine_t send_routine;
    gnss_location_callback_t location_callback;
    gnss_satellites_callback_t satellites_callback;
    void                *context;
} gnss_device_t;

static gnss_device_t gnss_device;

static void gnss_send_callback(void);
static void mtk_configure(gnss_device_t *device, unsigned int response, uint32_t command);
static void ubx_configure(gnss_device_t *device, unsigned int response, uint32_t command);

/************************************************************************************/

static const uint16_t utc_days_since_month[2][12] = {
    {   0,  31,  59,  90, 120, 151, 181, 212, 243, 273, 304, 334, },
    {   0,  31,  60,  91, 121, 152, 182, 213, 244, 274, 305, 335, },
};

static int utc_diff_time(const utc_time_t *t0, uint32_t offset0, const utc_time_t *t1, uint32_t offset1)
{
    /* The difference between t0 and t1 in seconds.
     */
  return (((((((((t0->year * 365 + (1 + ((t0->year -1) / 4))) +
		 utc_days_since_month[(t0->year & 3) == 0][t0->month -1] +
		 (t0->day -1)) -
		((t1->year * 365 + (1 + ((t1->year -1) / 4))) +
		   utc_days_since_month[(t1->year & 3) == 0][t1->month -1] +
		 (t1->day -1))) * 24) +
	      t0->hour -
	      t1->hour) * 60) +
	    t0->minute -
	    t1->minute) * 60) +
	  (t0->second + offset0) -
	  (t1->second + offset1));
}

/* utc_offset_time:
 *
 * Compute the UTC offset (or GPS leap second) by computing the elapsed UTC seconds
 * since 01/06/1980, and subtract that from week/tow (which is ahead by said 
 * leap seconds).
 */

static int utc_offset_time(const utc_time_t *time, uint16_t week, uint32_t tow)
{
    return ((((uint32_t)week * 604800) + ((tow + 500) / 1000)) -
	    ((((((((time->year * 365 + (1 + ((time->year -1) / 4))) +
		   utc_days_since_month[(time->year & 3) == 0][time->month -1] +
		   (time->day -1)) -
		  (6 -1) * 24) +
		 time->hour) * 60) +
	       time->minute) * 60) +
	     time->second));
}

/************************************************************************************/

static void gnss_location(gnss_device_t *device)
{
    switch (device->location.type) {
    case GNSS_LOCATION_TYPE_NONE:
	device->location.mask = 0;
	device->location.numsv = 0;
	device->location.quality = GNSS_LOCATION_QUALITY_NONE;
	break;

    case GNSS_LOCATION_TYPE_TIME:
	device->location.mask &= (GNSS_LOCATION_MASK_TIME |
				  GNSS_LOCATION_MASK_CORRECTION);

	device->location.quality = GNSS_LOCATION_QUALITY_NONE;
	break;

    case GNSS_LOCATION_TYPE_2D:
	device->location.mask &= (GNSS_LOCATION_MASK_TIME |
				  GNSS_LOCATION_MASK_CORRECTION |
				  GNSS_LOCATION_MASK_POSITION |
				  GNSS_LOCATION_MASK_SPEED |
				  GNSS_LOCATION_MASK_COURSE |
				  GNSS_LOCATION_MASK_EHPE |
				  GNSS_LOCATION_MASK_HDOP);
	break;

    case GNSS_LOCATION_TYPE_3D:
	break;

    default:
	break;
    }

    if (device->location.mask & GNSS_LOCATION_MASK_TIME)
    {
	if (!(device->location.mask & GNSS_LOCATION_MASK_CORRECTION))
	{
	    device->location.correction = 0;
	}
    }
    else
    {
	device->location.time.year   = 1980 - 1980;
	device->location.time.month  = 1;
	device->location.time.day    = 6;
	device->location.time.hour   = 0;
	device->location.time.minute = 0;
	device->location.time.second = 0;
	device->location.time.millis = 0;
	device->location.correction  = 0;

	device->location.mask = 0;
	device->location.numsv = 0;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_POSITION))
    {
	device->location.latitude = 0;
	device->location.longitude = 0;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_ALTITUDE))
    {
	device->location.altitude = 0;
	device->location.separation = 0;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_SPEED))
    {
	device->location.speed = 0;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_COURSE))
    {
	device->location.course = 0;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_CLIMB))
    {
	device->location.climb = 0;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_EHPE))
    {
	device->location.ehpe = 0;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_EVPE))
    {
	device->location.evpe = 0;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_PDOP))
    {
	device->location.pdop = 9999;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_HDOP))
    {
	device->location.hdop = 9999;
    }

    if (!(device->location.mask & GNSS_LOCATION_MASK_VDOP))
    {
	device->location.vdop = 9999;
    }

    if (device->location_callback)
    {
	(*device->location_callback)(device->context, &device->location);
    }

    device->location.type = 0;
    device->location.mask = 0;
}

static void gnss_satellites(gnss_device_t *device)
{
    if (device->satellites.count > GNSS_SATELLITES_COUNT_MAX)
    {
	device->satellites.count = GNSS_SATELLITES_COUNT_MAX;
    }

    if (device->satellites_callback)
    {
	(*device->satellites_callback)(device->context, &device->satellites);
    }
}

/************************************************************************************/

static const char nmea_hex_ascii[] = "0123456789ABCDEF";

static uint32_t nmea_isqrt(uint32_t n)
{  
    uint32_t c = 0x8000;  
    uint32_t g = 0x8000;  
  
    while (1)
    {  
	if((g*g) > n) 
	{
	    g ^= c;  
	}


	c >>= 1;  
	
	if(c == 0)  
	{
	    return g;  
	}

	g |= c;  
    }  
}  

static const uint32_t nmea_scale[10] = {
    1,
    10,
    100,
    1000,
    10000,
    100000,
    1000000,
    10000000,
    100000000,
    1000000000,
};

static int nmea_same_time(const utc_time_t *t0, const utc_time_t *t1)
{
    return ((t0->hour   == t1->hour) &&
	    (t0->minute == t1->minute) &&
	    (t0->second == t1->second) &&
	    (t0->millis == t1->millis));
}

static int nmea_parse_time(const uint8_t *data, utc_time_t *p_time)
{
    uint32_t hour, minute, second, millis, digits;

    if ((data[0] >= '0') && (data[0] <= '9') && (data[1] >= '0') && (data[1] <= '9'))
    {
	hour = (data[0] - '0') * 10 + (data[1] - '0');
	data += 2;
	
	if ((hour < 24) && (data[0] >= '0') && (data[0] <= '9') && (data[1] >= '0') && (data[1] <= '9'))
	{
	    minute = (data[0] - '0') * 10 + (data[1] - '0');
	    data += 2;
	    
	    if ((minute < 60) && (data[0] >= '0') && (data[0] <= '9') && (data[1] >= '0') && (data[1] <= '9'))
	    {
		second = (data[0] - '0') * 10 + (data[1] - '0');
		data += 2;
		
		/* A 60 is legal here for leap seconds.
		 */
		if (second <= 60)
		{
		    millis = 0;
		    
		    if (data[0] == '.')
		    {
			digits = 0;
			data++;
			
			while ((data[0] >= '0') && (data[0] <= '9'))
			{
			    if (digits < 3)
			    {
				millis = millis * 10 + (data[0] - '0');
				digits++;
			    }
			    data++;
			}
			
			if (data[0] == '\0')
			{
			    if (digits < 3)
			    {
				millis = millis * nmea_scale[3 - digits];
			    }
			}
		    }
		    
		    if (data[0] == '\0')
		    {
			p_time->hour   = hour;
			p_time->minute = minute;
			p_time->second = second;
			p_time->millis = millis;

			return 1;
		    }
		}
	    }
	}
    }

    return 0;
}

static int nmea_parse_unsigned(const uint8_t *data, uint32_t *p_unsigned)
{
    uint32_t integer;

    integer = 0;
                    
    while ((data[0] >= '0') && (data[0] <= '9'))
    {
	integer = integer * 10 + (data[0] - '0');
	data++;
    }

    if (data[0] == '\0')
    {
	*p_unsigned = integer;
        
	return 1;
    }

    return 0;
}

static int nmea_parse_fixed(const uint8_t *data, uint32_t *p_fixed, uint32_t scale)
{
    uint32_t integer, fraction, digits;

    integer = 0;
    
    while ((data[0] >= '0') && (data[0] <= '9'))
    {
	integer = integer * 10 + (data[0] - '0');
	data++;
    }
    
    
    fraction = 0;
    
    if (data[0] == '.')
    {
	digits = 0;
	data++;
        
	while ((data[0] >= '0') && (data[0] <= '9'))
	{
	    if (digits < scale)
	    {
		fraction = fraction * 10 + (data[0] - '0');
		digits++;
	    }
	    data++;
	}
        
	if (data[0] == '\0')
	{
	    if (digits < scale)
	    {
		fraction = fraction * nmea_scale[scale - digits];
	    }
	}
    }
    
    if (data[0] == '\0')
    {
	*p_fixed = integer * nmea_scale[scale] + fraction;
        
	return 1;
    }
    
    return 0;
}

static int nmea_parse_latitude(const uint8_t *data, uint32_t *p_latitude)
{
    uint32_t degrees, minutes;
    
    if ((data[0] >= '0') && (data[0] <= '9') && (data[1] >= '0') && (data[1] <= '9'))
    {
	degrees = (data[0] - '0') * 10 + (data[1] - '0');
	data += 2;
        
	if ((degrees < 90) && (data[0] != '\0') && nmea_parse_fixed(data, &minutes, 7))
	{
	    if (minutes < 600000000)
	    {
		*p_latitude = (uint32_t)(degrees * 10000000u) + (uint32_t)((minutes + 30) / 60);
		
		return 1;
	    }
	}
    }

    return 0;
}

static int nmea_parse_longitude(const uint8_t *data, uint32_t *p_longitude)
{
    uint32_t degrees, minutes;

    if ((data[0] >= '0') && (data[0] <= '9') && (data[1] >= '0') && (data[1] <= '9') && (data[2] >= '0') && (data[2] <= '9'))
    {
	degrees = (data[0] - '0') * 100 + (data[1] - '0') * 10 + (data[2] - '0');
	data += 3;
        
	if ((degrees < 180) && (data[0] != '\0') && nmea_parse_fixed(data, &minutes, 7))
	{
	    if (minutes < 600000000)
	    {
		*p_longitude = (uint32_t)(degrees * 10000000u) + (uint32_t)((minutes + 30) / 60);
		
		return 1;
	    }
	}
    }
    
    return 0;
}

static void nmea_start_sentence(gnss_device_t *device)
{
    nmea_context_t *context = &device->nmea;

    switch (context->sequence) {

    case NMEA_FIELD_SEQUENCE_GGA_END:
	break;

    case NMEA_FIELD_SEQUENCE_GSA_END:
	context->sv_used_count = 0;
	context->sv_used_mask[0] = 0;
	context->sv_used_mask[1] = 0;
	context->sv_used_mask[2] = 0;
	break;

    case NMEA_FIELD_SEQUENCE_GST_END:
	break;

    case NMEA_FIELD_SEQUENCE_GSV_END:
	context->sv_in_view_sentences = 0;
	break;

    case NMEA_FIELD_SEQUENCE_RMC_END:
	break;

    case NMEA_FIELD_SEQUENCE_PMTK001_END:
	break;

    default:
	break;
    }

    context->sequence = NMEA_FIELD_SEQUENCE_START;
}

static void nmea_parse_sentence(gnss_device_t *device, const uint8_t *data, unsigned int length)
{
    nmea_context_t *context = &device->nmea;
    uint32_t sequence, sequence_next;
    uint32_t latitude, longitude, altitude, separation, speed, course;
    uint32_t quality, stddev, pdop, hdop, vdop, date;
    uint32_t command, status;
    utc_time_t time;
    uint32_t count, sentences, current, svid, elevation, azimuth, snr;

    sequence = context->sequence;
    sequence_next = sequence +1;

    switch (sequence) {

    case NMEA_FIELD_SEQUENCE_START:
	sequence_next = NMEA_FIELD_SEQUENCE_SKIP;

	if (data[0] == 'P')
	{
	    if (!strcmp((const char*)data, "PMTK001"))
	    {
		sequence_next = NMEA_FIELD_SEQUENCE_PMTK001_COMMAND;
	    }
	}
	else
	{
	    if ((data[0] == 'G') && ((data[1] == 'P') || (data[1] == 'L') || (data[1] == 'N')))
	    {
		context ->prefix = data[1];

		/* --GSA is the switch detector in NMEA 0183. If it's GPGSA or GLGSA, then the system
		 * is set up as single GPS/GLONASS system, and we'd see only either a GPGSV or GLGSV
		 * later on. If it's a GNGSA, then another GNGSA will follow, one for GPS and one for 
		 * GLONASS. The constellation will be reported as GPGSV and GLGSV.
		 */
		if (!strcmp((const char*)(data+2), "GSA"))
		{
		    if (device->seen & NMEA_SENTENCE_MASK_GPGGA)
		    {
			sequence_next = NMEA_FIELD_SEQUENCE_GSA_OPERATION;
			
			context->mask = (NMEA_FIELD_MASK_PDOP | NMEA_FIELD_MASK_VDOP);
		    }
		}

		/* -- GSV is used to report the satellite constellation with either GPGSV or GLGSV.
		 * GNGSV is not legal.
		 */
		else if (!strcmp((const char*)(data+2), "GSV"))
		{
		    if (device->seen & (NMEA_SENTENCE_MASK_GPGGA | NMEA_SENTENCE_MASK_SOLUTION))
		    {
			sequence_next = NMEA_FIELD_SEQUENCE_GSV_SENTENCES;
		    }
		}

		/* According to the standard, if a receiver is supporting GPS only, the prefix would 
		 * be "GP". If it's a GLONASS only system, or a combined GPS+GLONASS system, then
		 * the prefix should be "GN". However some GLONASS only systems use a "GL" prefix,
		 * and quite a few GNSS_GLONASS systems mix "GP" and "GN" randomly. The system detection
		 * is done via --GSA anyway.
		 */
		else if (!strcmp((const char*)(data+2), "GGA"))
		{
		    sequence_next = NMEA_FIELD_SEQUENCE_GGA_TIME;
		    
		    /* GSA/GSV are subsequent to a GGA */
		    device->seen &= ~(NMEA_SENTENCE_MASK_GPGGA |
				      NMEA_SENTENCE_MASK_GPGSA |
				      NMEA_SENTENCE_MASK_GPGSV |
				      NMEA_SENTENCE_MASK_GLGSA |
				      NMEA_SENTENCE_MASK_GLGSV |
				      NMEA_SENTENCE_MASK_SOLUTION); 
		    
		    context->mask = (NMEA_FIELD_MASK_POSITION |
				     NMEA_FIELD_MASK_ALTITUDE |
				     NMEA_FIELD_MASK_HDOP);
		    
		    context->sv_in_view_sentences = 0;
			
		    context->sv_used_count = 0;
		    context->sv_used_mask[0] = 0;
		    context->sv_used_mask[1] = 0;
		    context->sv_used_mask[2] = 0;
			
		    device->satellites.count = 0;
		}

		else if (!strcmp((const char*)(data+2), "GST"))
		{
		    sequence_next = NMEA_FIELD_SEQUENCE_GST_TIME;
			
		    device->seen &= ~(NMEA_SENTENCE_MASK_GPGST | NMEA_SENTENCE_MASK_SOLUTION);
			
		    context->mask = (NMEA_FIELD_MASK_EHPE |
				     NMEA_FIELD_MASK_EVPE);
		}

		else if (!strcmp((const char*)(data+2), "RMC"))
		{
		    sequence_next = NMEA_FIELD_SEQUENCE_RMC_TIME;
			
		    device->seen &= ~(NMEA_SENTENCE_MASK_GPRMC | NMEA_SENTENCE_MASK_SOLUTION);
			
		    context->mask = (NMEA_FIELD_MASK_TIME |
				     NMEA_FIELD_MASK_SPEED |
				     NMEA_FIELD_MASK_COURSE);
		}
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_SKIP:
	sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	break;

    case NMEA_FIELD_SEQUENCE_GGA_TIME:
    case NMEA_FIELD_SEQUENCE_GST_TIME:
    case NMEA_FIELD_SEQUENCE_RMC_TIME:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_TIME;
	}
	else if (nmea_parse_time(data, &time))
	{
	    /* If there is a valid time stamp, and another sentence with a time stamp already been seen,
	     * make sure they have the same time. If not nuke the accumulated sentences.
	     */

	    if (device->seen & (NMEA_SENTENCE_MASK_GPGGA | NMEA_SENTENCE_MASK_GPGST | NMEA_SENTENCE_MASK_GPRMC))
	    {
		if (!nmea_same_time(&device->location.time, &time))
		{
		    device->seen = 0;
		    device->location.type = 0;
		    device->location.mask = 0;
		}
	    }

	    device->location.time.hour   = time.hour;
	    device->location.time.minute = time.minute;
	    device->location.time.second = time.second;
	    device->location.time.millis = time.millis;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_LATITUDE:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_POSITION;
	}
	else if (nmea_parse_latitude(data, &latitude))
	{
	    device->location.latitude = latitude;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_LATITUDE_NS:
	if (context->mask & NMEA_FIELD_MASK_POSITION)
	{
	    if (data[0] == 'S')
	    {
		device->location.latitude = - device->location.latitude;
	    }
	    else if (data[0] != 'N')
	    {
		sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_LONGITUDE:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_POSITION;
	}
	else if (nmea_parse_longitude(data, &longitude))
	{
	    device->location.longitude = longitude;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_LONGITUDE_EW:
	if (context->mask & NMEA_FIELD_MASK_POSITION)
	{
	    if (data[0] == 'W')
	    {
		device->location.longitude = - device->location.longitude;
	    }
	    else if (data[0] != 'E')
	    {
		sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_QUALITY:
	if ((data[0] == '\0') || !nmea_parse_unsigned(data, &quality))
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	else
	{
	    device->location.quality = quality;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_HDOP:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_HDOP;
	}
	else if (nmea_parse_fixed(data, &hdop, 2))
	{
	    device->location.hdop = hdop;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_ALTITUDE:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_ALTITUDE;
	}
	else if (nmea_parse_fixed(((data[0] == '-') ? data+1 : data), &altitude, 3))
	{
	    if (data[0] == '-')
	    {
		device->location.altitude = -altitude;
	    }
	    else
	    {
		device->location.altitude = altitude;
	    }
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_ALTITUDE_UNIT:
	if (context->mask & NMEA_FIELD_MASK_ALTITUDE)
	{
	    if (data[0] != 'M')
	    {
		sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_SEPARATION:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_ALTITUDE;
	}
	else if (nmea_parse_fixed(((data[0] == '-') ? data+1 : data), &separation, 3))
	{
	    if (context->mask & NMEA_FIELD_MASK_ALTITUDE)
	    {
		if (data[0] == '-')
		{
		    device->location.separation = -separation;
		}
		else
		{
		    device->location.separation = separation;
		}
	    }
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_SEPARATION_UNIT:
	if (context->mask & NMEA_FIELD_MASK_ALTITUDE)
	{
	    if (data[0] != 'M')
	    {
		sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_GGA_NUMSV:
    case NMEA_FIELD_SEQUENCE_GGA_DIFFERENTIAL_AGE:
    case NMEA_FIELD_SEQUENCE_GSA_OPERATION:
    case NMEA_FIELD_SEQUENCE_GSA_HDOP:
    case NMEA_FIELD_SEQUENCE_GST_RANGE:
    case NMEA_FIELD_SEQUENCE_GST_STDDEV_MAJOR:
    case NMEA_FIELD_SEQUENCE_GST_STDDEV_MINOR:
    case NMEA_FIELD_SEQUENCE_GST_ORIENTATION:
    case NMEA_FIELD_SEQUENCE_RMC_LATITUDE:
    case NMEA_FIELD_SEQUENCE_RMC_LATITUDE_NS:
    case NMEA_FIELD_SEQUENCE_RMC_LONGITUDE:
    case NMEA_FIELD_SEQUENCE_RMC_LONGITUDE_EW:
    case NMEA_FIELD_SEQUENCE_RMC_VARIATION:
    case NMEA_FIELD_SEQUENCE_RMC_VARIATION_UNIT:
	/* SKIP FIELD */
	break;

    case NMEA_FIELD_SEQUENCE_GGA_DIFFERENTIAL_STATION:
	/* SKIP FIELD */
	sequence_next = NMEA_FIELD_SEQUENCE_GGA_END;
	break;

    case NMEA_FIELD_SEQUENCE_GSA_NAVIGATION:
	if (data[0] == '1')
	{
	    context->navigation = NMEA_NAVIGATION_NONE;
	}
	else if (data[0] == '2')
	{
	    context->navigation = NMEA_NAVIGATION_2D;
	}
	else if (data[0] == '3')
	{
	    context->navigation = NMEA_NAVIGATION_3D;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_1:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_2:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_3:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_4:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_5:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_6:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_7:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_8:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_9:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_10:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_11:
    case NMEA_FIELD_SEQUENCE_GSA_SV_USED_PRN_12:
	if (data[0] != '\0')
	{
	    if (nmea_parse_unsigned(data, &svid))
	    {
		if ((svid >= 1) && (svid <= 96))
		{
		    context->sv_used_mask[(svid -1) >> 5] |= (1ul << ((svid -1) & 31));
		}
	    }
	    else
	    {
		context->sv_used_count = 0;
		context->sv_used_mask[0] = 0;
		context->sv_used_mask[1] = 0;
		context->sv_used_mask[2] = 0;

		sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSA_PDOP:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_PDOP;
	}
	else if (nmea_parse_fixed(data, &pdop, 2))
	{
	    device->location.pdop = pdop;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSA_VDOP:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_VDOP;

	    sequence_next = NMEA_FIELD_SEQUENCE_GSA_END;
	}
	else if (nmea_parse_fixed(data, &vdop, 2))
	{
	    device->location.vdop = vdop;
	    
	    sequence_next = NMEA_FIELD_SEQUENCE_GSA_END;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GST_STDDEV_LATITUDE:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_EHPE;
	}
	else if (nmea_parse_fixed(data, &stddev, 3))
	{
	    device->location.ehpe = stddev;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GST_STDDEV_LONGITUDE:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_EHPE;
	}
	else if (nmea_parse_fixed(data, &stddev, 3))
	{
	    device->location.ehpe = nmea_isqrt((device->location.ehpe * device->location.ehpe) + (stddev * stddev));
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GST_STDDEV_ALTITUDE:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_EVPE;

	    sequence_next = NMEA_FIELD_SEQUENCE_GST_END;
	}
	else if (nmea_parse_fixed(data, &stddev, 3))
	{
	    device->location.evpe = stddev;
	    
	    sequence_next = NMEA_FIELD_SEQUENCE_GST_END;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSV_SENTENCES:
	if ((data[0] == '\0') || !nmea_parse_unsigned(data, &sentences))
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	else
	{
	    if (context->sv_in_view_sentences == 0)
	    {
		context->sv_in_view_sentences = sentences;
		context->sv_in_view_count = 0;
		context->sv_in_view_index = 0;
	    }
	    else
	    {
		if (context->sv_in_view_sentences != sentences)
		{
		    context->sv_in_view_sentences = 0;

		    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
		}
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSV_CURRENT:
	if ((data[0] == '\0') || !nmea_parse_unsigned(data, &current))
	{
	    context->sv_in_view_sentences = 0;

	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	else
	{
	    if (context->sv_in_view_index != ((current -1) << 2))
	    {
		context->sv_in_view_sentences = 0;

		sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_COUNT:
	if ((data[0] == '\0') || !nmea_parse_unsigned(data, &count))
	{
	    context->sv_in_view_sentences = 0;

	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	else
	{
	    context->sv_in_view_count = count;

	    if (count == 0)
	    {
		sequence_next = NMEA_FIELD_SEQUENCE_GSV_END;
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_ID:
	svid = 255;

	if ((data[0] == '\0') || nmea_parse_unsigned(data, &svid))
	{
	    if (device->satellites.count < GNSS_SATELLITES_COUNT_MAX)
	    {
		device->satellites.info[device->satellites.count].prn = svid;
		device->satellites.info[device->satellites.count].state = GNSS_SATELLITES_STATE_SEARCHING;
		device->satellites.info[device->satellites.count].snr = 0;
		device->satellites.info[device->satellites.count].elevation = 0;
		device->satellites.info[device->satellites.count].azimuth = 0;
	    }
	}
	else
	{
	    context->sv_in_view_sentences = 0;

	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_ELEV:
	elevation = 0;

	if ((data[0] == '\0') || nmea_parse_unsigned(data, &elevation))
	{
	    if (device->satellites.count < GNSS_SATELLITES_COUNT_MAX)
	    {
		device->satellites.info[device->satellites.count].elevation = elevation;
	    }
	}
	else
	{
	    context->sv_in_view_sentences = 0;

	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_AZIM:
	azimuth = 0;

	if ((data[0] == '\0') || nmea_parse_unsigned(data, &azimuth))
	{
	    if (device->satellites.count < GNSS_SATELLITES_COUNT_MAX)
	    {
		device->satellites.info[device->satellites.count].azimuth = azimuth;
	    }
	}
	else
	{
	    context->sv_in_view_sentences = 0;

	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_SNR:
	if ((data[0] == '\0') || nmea_parse_unsigned(data, &snr))
	{
	    if (device->satellites.count < GNSS_SATELLITES_COUNT_MAX)
	    {
		if (data[0] != '\0')
		{
		    device->satellites.info[device->satellites.count].state = GNSS_SATELLITES_STATE_TRACKING;
		    device->satellites.info[device->satellites.count].snr = snr;
		}
	    }

	    device->satellites.count++;

	    context->sv_in_view_index++;

	    if ((context->sv_in_view_index == context->sv_in_view_count) || !(context->sv_in_view_index & 3))
	    {
		sequence_next = NMEA_FIELD_SEQUENCE_GSV_END;
	    }
	    else
	    {
		sequence_next = NMEA_FIELD_SEQUENCE_GSV_SV_IN_VIEW_ID;
	    }
	}
	else
	{
	    context->sv_in_view_sentences = 0;

	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_RMC_STATUS:
	if (data[0] == 'A')
	{
	    context->status = NMEA_STATUS_DATA_VALID;
	}
	else if (data[0] == 'V')
	{
	    context->status = NMEA_STATUS_RECEIVER_WARNING;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_RMC_SPEED:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_SPEED;
	}
	else if (nmea_parse_fixed(data, &speed, 3))
	{
	    /* Conversion factor from knots to m/s is 1852 / 3600.
	     */
	    device->location.speed  = (speed * 1852 + 1800) / 3600;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_RMC_COURSE:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_COURSE;
	}
	else if (nmea_parse_fixed(data, &course, 5))
	{
	    device->location.course = course;
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_RMC_DATE:
	if (data[0] == '\0')
	{
	    context->mask &= ~NMEA_FIELD_MASK_TIME;
	}
	else if (nmea_parse_unsigned(data, &date))
	{
	    device->location.time.day   = date / 10000;
	    device->location.time.month = (date - device->location.time.day * 10000) / 100;
	    device->location.time.year  = (date - device->location.time.day * 10000 - device->location.time.month * 100);
	    
	    if (device->location.time.year < 80)
	    {
		device->location.time.year = (2000 + device->location.time.year) - 1980;
	    }
	    else
	    {
		device->location.time.year = (1900 + device->location.time.year) - 1980;
	    }
	}
	else
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	break;

    case NMEA_FIELD_SEQUENCE_RMC_MODE:
	/* SKIP FIELD */
	sequence_next = NMEA_FIELD_SEQUENCE_RMC_END;
	break;

    case NMEA_FIELD_SEQUENCE_PMTK001_COMMAND:
	if ((data[0] == '\0') || !nmea_parse_unsigned(data, &command))
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	else
	{
	    context->mtk_command = command;
	}
	break;

    case NMEA_FIELD_SEQUENCE_PMTK001_STATUS:
	if ((data[0] == '\0') || !nmea_parse_unsigned(data, &status))
	{
	    sequence_next = NMEA_FIELD_SEQUENCE_SKIP;
	}
	else
	{
	    context->mtk_status = status;

	    sequence_next = NMEA_FIELD_SEQUENCE_PMTK001_END;
	}
	break;
    }

    context->sequence = sequence_next;
}

static void nmea_end_sentence(gnss_device_t *device)
{
    nmea_context_t *context = &device->nmea;
    uint32_t expected;
    uint32_t n, svid;

    switch (context->sequence) {

    case NMEA_FIELD_SEQUENCE_GGA_END:
      if (context->mask & NMEA_FIELD_MASK_POSITION)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_POSITION;
	}

	if (context->mask & NMEA_FIELD_MASK_ALTITUDE)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_ALTITUDE;
	}

	if (context->mask & NMEA_FIELD_MASK_HDOP)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_HDOP;
	}
	
	device->seen |= NMEA_SENTENCE_MASK_GPGGA;
	device->seen &= ~NMEA_SENTENCE_MASK_SOLUTION;
	break;

    case NMEA_FIELD_SEQUENCE_GSA_END:
	if (context->mask & NMEA_FIELD_MASK_PDOP)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_PDOP;
	}

	if (context->mask & NMEA_FIELD_MASK_VDOP)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_VDOP;
	}

	/* If the talkes is "GN", then it's a composite fix, which will consist out
	 * of GNGSA, GNGSA, GPGSV & GLGSV. Otherwise only a GPGSA & GPGSV is to be
	 * expected.
	 */

	if (context->prefix == 'N')
	{
	    device->expected |= (NMEA_SENTENCE_MASK_GPGSA | NMEA_SENTENCE_MASK_GPGSV | NMEA_SENTENCE_MASK_GLGSA | NMEA_SENTENCE_MASK_GLGSV);
	    
	    if (!(device->seen & NMEA_SENTENCE_MASK_GPGSA))
	    {
		device->seen |= NMEA_SENTENCE_MASK_GPGSA;
	    }
	    else
	    {
		device->seen |= NMEA_SENTENCE_MASK_GLGSA;
		device->seen &= ~NMEA_SENTENCE_MASK_SOLUTION;
	    }
	}
	else if (context->prefix == 'L')
	{
	    device->expected = (device->expected & ~(NMEA_SENTENCE_MASK_GPGSA | NMEA_SENTENCE_MASK_GPGSV)) | (NMEA_SENTENCE_MASK_GLGSA | NMEA_SENTENCE_MASK_GLGSV);

	    device->seen |= NMEA_SENTENCE_MASK_GLGSA;
	    device->seen &= ~NMEA_SENTENCE_MASK_SOLUTION;
	}
	else
	{
	    device->expected = (device->expected & ~(NMEA_SENTENCE_MASK_GLGSA | NMEA_SENTENCE_MASK_GLGSV)) | (NMEA_SENTENCE_MASK_GPGSA | NMEA_SENTENCE_MASK_GPGSV);

	    device->seen |= NMEA_SENTENCE_MASK_GPGSA;
	    device->seen &= ~NMEA_SENTENCE_MASK_SOLUTION;
	}
	break;

    case NMEA_FIELD_SEQUENCE_GST_END:
	device->expected |= NMEA_SENTENCE_MASK_GPGST;

	if (context->mask & NMEA_FIELD_MASK_EHPE)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_EHPE;
	}

	if (context->mask & NMEA_FIELD_MASK_EVPE)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_EVPE;
	}

	device->seen |= NMEA_SENTENCE_MASK_GPGST;
	device->seen &= ~NMEA_SENTENCE_MASK_SOLUTION;
	break;

    case NMEA_FIELD_SEQUENCE_GSV_END:
	if (context->sv_in_view_count == context->sv_in_view_index)
	{
	    context->sv_in_view_sentences = 0;

	    if (context->prefix == 'P')
	    {
		device->seen |= NMEA_SENTENCE_MASK_GPGSV;
	    }

	    if (context->prefix == 'L')
	    {
		device->seen |= NMEA_SENTENCE_MASK_GLGSV;
	    }
	}
	break;

    case NMEA_FIELD_SEQUENCE_RMC_END:
	if (context->mask & NMEA_FIELD_MASK_TIME)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_TIME;
	}

	if (context->mask & NMEA_FIELD_MASK_SPEED)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_SPEED;
	}

	if (context->mask & NMEA_FIELD_MASK_COURSE)
	{
	    device->location.mask |= GNSS_LOCATION_MASK_COURSE;
	}

	device->seen |= NMEA_SENTENCE_MASK_GPRMC;
	device->seen &= ~NMEA_SENTENCE_MASK_SOLUTION;
	break;

    case NMEA_FIELD_SEQUENCE_PMTK001_END:
	if (device->command == context->mtk_command)
	{
	    device->command = ~0l;

	    mtk_configure(device, (context->mtk_status == 3) ? GNSS_RESPONSE_ACK : GNSS_RESPONSE_NACK, context->mtk_command);
	}
	break;

    default:
	break;
    }

    context->sequence = NMEA_FIELD_SEQUENCE_START;

    if (device->init == GNSS_INIT_DONE)
    {
	expected = device->expected & (NMEA_SENTENCE_MASK_GPGGA |
				       NMEA_SENTENCE_MASK_GPGSA |
				       NMEA_SENTENCE_MASK_GPGST |
				       NMEA_SENTENCE_MASK_GPRMC |
				       NMEA_SENTENCE_MASK_GLGSA);
	
	if ((device->seen & expected) == expected)
	{
	    if ((context->status == NMEA_STATUS_DATA_VALID) && (context->navigation != NMEA_NAVIGATION_NONE))
	    {
		device->location.type = (context->navigation == NMEA_NAVIGATION_2D) ? GNSS_LOCATION_TYPE_2D : GNSS_LOCATION_TYPE_3D;
		device->location.numsv = context->sv_used_count;
	    }
	    else
	    {
		device->location.type = GNSS_LOCATION_TYPE_NONE;
		device->location.numsv = 0;

		context->sv_used_count   = 0;
		context->sv_used_mask[0] = 0;
		context->sv_used_mask[1] = 0;
		context->sv_used_mask[2] = 0;
	    }

	    gnss_location(device);
	    
	    device->seen &= ~(NMEA_SENTENCE_MASK_GPGGA |
			      NMEA_SENTENCE_MASK_GPGSA |
			      NMEA_SENTENCE_MASK_GPGST |
			      NMEA_SENTENCE_MASK_GPRMC |
			      NMEA_SENTENCE_MASK_GLGSA);

	    device->seen |= NMEA_SENTENCE_MASK_SOLUTION;
	}
	
	
	expected = device->expected & (NMEA_SENTENCE_MASK_GPGSV | NMEA_SENTENCE_MASK_GLGSV);
	
	if ((device->seen & NMEA_SENTENCE_MASK_SOLUTION) && ((device->seen & expected) == expected))
	{
	    for (n = 0; n < device->satellites.count; n++)
	    {
		svid = device->satellites.info[n].prn;

		if ((svid >= 1) && (svid <= 96) && (context->sv_used_mask[(svid -1) >> 5] & (1ul << ((svid -1) & 31))))
		{
		    device->satellites.info[n].state |= GNSS_SATELLITES_STATE_NAVIGATING;
		}
	    }

	    gnss_satellites(device);
	    
	    device->seen &= ~(NMEA_SENTENCE_MASK_GPGSV | NMEA_SENTENCE_MASK_GLGSV);
	}
    }
}

static const uint8_t * const mtk_init_table_1hz[] = {
    (const uint8_t *)"$PMTK314,0,1,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0*28\r\n",
    (const uint8_t *)"$PMTK220,1000*1F\r\n",            /* POS FIX       */
    (const uint8_t *)"$PMTK300,1000,0,0,0,0*1C\r\n",    /* FIX CTL       */
    (const uint8_t *)"$PMTK286,1*23\r\n",               /* AIC           */
    (const uint8_t *)"$PMTK397,0*23\r\n",               /* NAV THRESHOLD */
    NULL,
};

static const uint8_t * const mtk_init_table_5hz[] = {
    (const uint8_t *)"$PMTK314,0,1,0,1,1,5,1,1,0,0,0,0,0,0,0,0,0,0,0*2C\r\n",
    (const uint8_t *)"$PMTK220,200*2C\r\n",             /* POS FIX       */
    (const uint8_t *)"$PMTK300,200,0,0,0,0*2F\r\n",     /* FIX CTL       */
    (const uint8_t *)"$PMTK286,1*23\r\n",               /* AIC           */
    (const uint8_t *)"$PMTK397,0*23\r\n",               /* NAV THRESHOLD */
    NULL,
};

static const uint8_t * const mtk_constellation_gps_glonass_table[] = {
    (const uint8_t *)"$PMTK353,1,1*37\r\n",             /* GLONASS       */
    NULL,
};

static const uint8_t * const mtk_constellation_gps_table[] = {
    (const uint8_t *)"$PMTK353,1,0*36\r\n",             /* GLONASS       */
    NULL,
};

static const uint8_t * const mtk_sbas_enable_table[] = {
    (const uint8_t *)"$PMTK301,2*2E\r\n",               /* DGPS MODE     */
    (const uint8_t *)"$PMTK313,1*2E\r\n",               /* SBAS ENABLED  */
    NULL,
};

static const uint8_t * const mtk_sbas_disable_table[] = {
    (const uint8_t *)"$PMTK301,0*2C\r\n",               /* DGPS MODE     */
    (const uint8_t *)"$PMTK313,0*2F\r\n",               /* SBAS ENABLED  */
    NULL,
};

static const uint8_t * const mtk_qzss_enable_table[] = {
    (const uint8_t *)"$PMTK351,0*29\r\n",               /* QZSS NMEA     */
    (const uint8_t *)"$PMTK352,0*2A\r\n",               /* QZSS STOP     */
    NULL,
};

static const uint8_t * const mtk_qzss_disable_table[] = {
    (const uint8_t *)"$PMTK351,0*29\r\n",               /* QZSS NMEA     */
    (const uint8_t *)"$PMTK352,1*2B\r\n",               /* QZSS STOP     */
    NULL,
};

static void mtk_send(gnss_device_t *device, const uint8_t *data)
{
    device->command = ((data[5] - '0') * 10 + (data[6] - '0')) * 10 + (data[7] - '0');
    device->busy = 1;

    (*device->send_routine)(device->context, data, strlen((const char*)data), gnss_send_callback);
}

static void mtk_table(gnss_device_t *device, const uint8_t * const * table)
{
    const uint8_t *data;

    data = table[0];
    device->table = table + 1;

    mtk_send(device, data);
}

static void mtk_configure(gnss_device_t *device, unsigned int response, uint32_t command)
{
    const uint8_t *data = NULL;

    if (device->table)
    {
	if (device->init == GNSS_INIT_MTK_BAUD_RATE)
	{
	    device->init = GNSS_INIT_MTK_INIT_TABLE;

	    data = device->table[0];
	    device->table = device->table + 1;
	}
	else
	{
	    if (device->table[0] != NULL)
	    {
		data = device->table[0];
		device->table = device->table + 1;
	    }
	    else
	    {
		device->table = NULL;

		if (device->init == GNSS_INIT_MTK_INIT_TABLE)
		{
		    device->init = GNSS_INIT_DONE;
		    device->seen = 0;
		    device->expected = NMEA_SENTENCE_MASK_GPGGA | NMEA_SENTENCE_MASK_GPGSA | NMEA_SENTENCE_MASK_GPGSV | NMEA_SENTENCE_MASK_GPRMC;
		
		    device->location.type = 0;
		    device->location.mask = 0;
		}
	    }
	}
    }

    if (data)
    {
	mtk_send(device, data);
    }
}

/************************************************************************************/

#if 0

static inline int8_t ubx_data_int8(const uint8_t *data, unsigned int offset)
{
    return (int8_t)data[offset];
}

static inline int16_t ubx_data_int16(const uint8_t *data, unsigned int offset)
{
    return (int16_t)(((uint16_t)data[offset+0] << 0) |
		     ((uint16_t)data[offset+1] << 8));
}

static inline int32_t ubx_data_int32(const uint8_t *data, unsigned int offset)
{
    return (int32_t)(((uint32_t)data[offset+0] <<  0) |
		     ((uint32_t)data[offset+1] <<  8) |
		     ((uint32_t)data[offset+2] << 16) |
		     ((uint32_t)data[offset+3] << 24));
}

static inline uint8_t ubx_data_uint8(const uint8_t *data, unsigned int offset)
{
    return (uint8_t)data[offset];
}

static inline uint16_t ubx_data_uint16(const uint8_t *data, unsigned int offset)
{
    return (uint16_t)(((uint16_t)data[offset+0] << 0) |
		      ((uint16_t)data[offset+1] << 8));
}

static inline uint32_t ubx_data_uint32(const uint8_t *data, unsigned int offset)
{
    return (uint32_t)(((uint32_t)data[offset+0] <<  0) |
		      ((uint32_t)data[offset+1] <<  8) |
		      ((uint32_t)data[offset+2] << 16) |
		      ((uint32_t)data[offset+3] << 24));
}

#endif


static inline int8_t ubx_data_int8(const uint8_t *data, unsigned int offset)
{
    return *((const int8_t*)((const void*)(&data[offset])));
}

static inline int16_t ubx_data_int16(const uint8_t *data, unsigned int offset)
{
    return *((const int16_t*)((const void*)(&data[offset])));
}

static inline int32_t ubx_data_int32(const uint8_t *data, unsigned int offset)
{
    return *((const int32_t*)((const void*)(&data[offset])));
}

static inline uint8_t ubx_data_uint8(const uint8_t *data, unsigned int offset)
{
    return *((const uint8_t*)((const void*)(&data[offset])));
}

static inline uint16_t ubx_data_uint16(const uint8_t *data, unsigned int offset)
{
    return *((const uint16_t*)((const void*)(&data[offset])));
}

static inline uint32_t ubx_data_uint32(const uint8_t *data, unsigned int offset)
{
    return *((const uint32_t*)((const void*)(&data[offset])));
}

static void ubx_start_message(gnss_device_t *device, unsigned int message, unsigned int length)
{
    if (message == 0x0130)
    {
	/* UBX-NAV-SVINFO/UBX-NAV-SAT */
	    
	device->rx_chunk = 20;
	device->satellites.count = 0;
	
	device->seen &= ~UBX_MESSAGE_MASK_NAV_SVINFO;
    }
}

static void ubx_parse_message(gnss_device_t *device, unsigned int message, uint8_t *data, unsigned int count)
{
    unsigned int svid;

    if (message == 0x0130)
    {
	/* UBX-NAV-SVINFO */

	svid = ubx_data_uint8(data, 9);

	if ((svid >= 1) && (svid <= 32))
	{
	    /* GPS */
	}
	else if ((svid >= 33) && (svid <= 64))
	{
	    /* BEIDOU */
	    svid += (201 + 5 - 33);
	}
	else if ((svid >= 65) && (svid <= 96))
	{
	    /* GLONASS */
	}
	else if ((svid >= 120) && (svid <= 151))
	{
	    /* SBAS */
	    svid -= 87;
	}
	else if ((svid >= 152) && (svid <= 158))
	{
	    /* SBAS */
	}
	else if ((svid >= 159) && (svid <= 163))
	{
	    /* BEIDOU */
	    svid += (201 - 159);
	}
	else if ((svid >= 193) && (svid <= 200))
	{
	    /* QZSS */
	}
	else if (svid == 255)
	{
	    /* GLONASS */
	}
	else
	{
	    svid = 0;
	}

	if (svid && (device->satellites.count < GNSS_SATELLITES_COUNT_MAX))
	{
	    device->satellites.info[device->satellites.count].prn = svid;

	    if (ubx_data_int8(data, 13) > 0)
	    {
		device->satellites.info[device->satellites.count].elevation = ubx_data_int8(data, 13);
		device->satellites.info[device->satellites.count].azimuth = ubx_data_int16(data, 14);
	    }
	    else
	    {
		device->satellites.info[device->satellites.count].elevation = 0;
		device->satellites.info[device->satellites.count].azimuth = 0;
	    }

	    device->satellites.info[device->satellites.count].snr = ubx_data_uint8(data, 12);

	    switch (ubx_data_uint8(data, 11) & 0x0f) {
	    case 0x00: /* NO SIGNAL */
	    case 0x01: /* SEARCHING */ 
		device->satellites.info[device->satellites.count].state = GNSS_SATELLITES_STATE_SEARCHING;
		break;

	    case 0x02: /* SIGNAL ACQUIRED */
	    case 0x03: /* SIGNAL ACQUIRED, BUT UNUSABLE */
	    case 0x04: /* SIGNAL ACQUIRED, CODE LOCKED */
	    case 0x05: /* SIGNAL ACQUIRED, CODE LOCKED, CARRIER LOCKED */
	    case 0x06: /* SIGNAL ACQUIRED, CODE LOCKED, CARRIER LOCKED */
	    case 0x07: /* SIGNAL ACQUIRED, CODE LOCKED, CARRIER LOCKED */
		device->satellites.info[device->satellites.count].state = GNSS_SATELLITES_STATE_TRACKING;
		break;

	    default:
		device->satellites.info[device->satellites.count].state = GNSS_SATELLITES_STATE_SEARCHING;
		break;
	    }

	    if (device->satellites.info[device->satellites.count].state & GNSS_SATELLITES_STATE_TRACKING)
	    {
		if (ubx_data_uint8(data, 10) & 0x01)
		{
		    device->satellites.info[device->satellites.count].state |= GNSS_SATELLITES_STATE_NAVIGATING;
		}

		if (ubx_data_uint8(data, 10) & 0x02)
		{
		    device->satellites.info[device->satellites.count].state |= GNSS_SATELLITES_STATE_CORRECTION;
		}
	    }

	    device->satellites.count++;
	}
	
	device->rx_offset += 12;
	device->rx_chunk += 12;
    }
}

static void ubx_end_message(gnss_device_t *device, unsigned int message, uint8_t *data, unsigned int count)
{
    ubx_context_t *context = &device->ubx;
    unsigned int expected;
    uint16_t week, command;
    int32_t tow;

    if ((message >> 8) == 0x01)
    {
	if (device->seen & (UBX_MESSAGE_MASK_NAV_DOP |
			    UBX_MESSAGE_MASK_NAV_PVT |
			    UBX_MESSAGE_MASK_NAV_SVINFO |
			    UBX_MESSAGE_MASK_NAV_TIMEGPS |
			    UBX_MESSAGE_MASK_SOLUTION))
	{
	    if (context->itow != ubx_data_uint32(data, 0))
	    {
		device->seen = 0;
		device->location.type = 0;
		device->location.mask = 0;
	    }
	}
	
	context->itow = ubx_data_uint32(data, 0);

	switch (message & 0xff) {

	case 0x04:
	    /* UBX-NAV-DOP */

	    device->location.pdop = ubx_data_uint16(data, 6);
	    device->location.hdop = ubx_data_uint16(data, 12);
	    device->location.vdop = ubx_data_uint16(data, 10);

	    device->location.mask |= (GNSS_LOCATION_MASK_PDOP |
				      GNSS_LOCATION_MASK_HDOP |
				      GNSS_LOCATION_MASK_VDOP);

	    device->seen |= UBX_MESSAGE_MASK_NAV_DOP;
	    break;

	case 0x07:
	    /* UBX-NAV-PVT */

	    if ((ubx_data_uint8(data, 11) & 0x03) == 0x03)
	    {
		device->location.time.year = ubx_data_uint16(data, 4) - 1980;
		device->location.time.month = ubx_data_uint8(data, 6);
		device->location.time.day = ubx_data_uint8(data, 7);
		device->location.time.hour = ubx_data_uint8(data, 8);
		device->location.time.minute = ubx_data_uint8(data, 9);
		device->location.time.second = ubx_data_uint8(data, 10);

		if (ubx_data_int32(data, 16) > 0)
		{
		    device->location.time.millis = ((ubx_data_int32(data, 16) + 500000) / 1000000);
		}
		else
		{
		    device->location.time.millis = 0;
		}
	    }
	    else
	    {
		device->location.time.year = 1980 - 1980;
		device->location.time.month = 1;
		device->location.time.day = 6;
		device->location.time.hour = 0;
		device->location.time.minute = 0;
		device->location.time.second = 0;
		device->location.time.millis = 0;
	    }

	    device->location.latitude = ubx_data_int32(data, 28);
	    device->location.longitude = ubx_data_int32(data, 24);
	    device->location.altitude = ubx_data_int32(data, 36);
	    device->location.separation = ubx_data_int32(data, 32) - ubx_data_int32(data, 36);
	    device->location.speed = ubx_data_int32(data, 60);
	    device->location.course = ubx_data_int32(data, 64);
	    device->location.climb = - ubx_data_int32(data, 56);
	    device->location.ehpe = ubx_data_uint32(data, 40);
	    device->location.evpe = ubx_data_uint32(data, 44);

	    switch (ubx_data_uint8(data, 20)) {
	    case 0x00:
		device->location.type = GNSS_LOCATION_TYPE_NONE;
		device->location.quality = GNSS_LOCATION_QUALITY_NONE;
		break;

	    case 0x01:
		device->location.type = GNSS_LOCATION_TYPE_NONE;
		device->location.quality = GNSS_LOCATION_QUALITY_ESTIMATED;
		break;

	    case 0x02:
		device->location.type = GNSS_LOCATION_TYPE_2D;

		if (ubx_data_uint8(data, 21) & 0xc0)
		{
		    device->location.quality = ((ubx_data_uint8(data, 21) & 0x80) ? GNSS_LOCATION_QUALITY_RTK_FIXED : GNSS_LOCATION_QUALITY_RTK_FLOAT);
		}
		else if (ubx_data_uint8(data, 21) & 0x01)
		{
		    device->location.quality = ((ubx_data_uint8(data, 21) & 0x02) ? GNSS_LOCATION_QUALITY_DIFFERENTIAL : GNSS_LOCATION_QUALITY_AUTONOMOUS);
		}
		else
		{
		    device->location.quality = GNSS_LOCATION_QUALITY_NONE;
		}
		break;

	    case 0x03:
		device->location.type = GNSS_LOCATION_TYPE_3D;

		if (ubx_data_uint8(data, 21) & 0xc0)
		{
		    device->location.quality = ((ubx_data_uint8(data, 21) & 0x80) ? GNSS_LOCATION_QUALITY_RTK_FIXED : GNSS_LOCATION_QUALITY_RTK_FLOAT);
		}
		else if (ubx_data_uint8(data, 21) & 0x01)
		{
		    device->location.quality = ((ubx_data_uint8(data, 21) & 0x02) ? GNSS_LOCATION_QUALITY_DIFFERENTIAL : GNSS_LOCATION_QUALITY_AUTONOMOUS);
		}
		else
		{
		    device->location.quality = GNSS_LOCATION_QUALITY_NONE;
		}
		break;

	    case 0x04:
		device->location.type = GNSS_LOCATION_TYPE_2D;
		device->location.quality = GNSS_LOCATION_QUALITY_ESTIMATED;
		break;

	    case 0x05:
		device->location.type = GNSS_LOCATION_TYPE_TIME;
		device->location.quality = GNSS_LOCATION_QUALITY_NONE;
		break;
	    }

	    device->location.numsv = ubx_data_uint8(data, 23);

	    device->location.mask |= (GNSS_LOCATION_MASK_POSITION |
				      GNSS_LOCATION_MASK_ALTITUDE |
				      GNSS_LOCATION_MASK_SPEED |
				      GNSS_LOCATION_MASK_COURSE |
				      GNSS_LOCATION_MASK_CLIMB |
				      GNSS_LOCATION_MASK_EHPE |
				      GNSS_LOCATION_MASK_EVPE);

	    device->seen |= UBX_MESSAGE_MASK_NAV_PVT;
	    device->seen &= ~UBX_MESSAGE_MASK_SOLUTION;
	    break;

	case 0x20:
	    /* UBX-NAV-TIMEGPS */

	    if ((ubx_data_uint8(data, 11) & 0x03) == 0x03)
	    {
		tow  = ubx_data_uint32(data, 0) + (ubx_data_int32(data, 4) + 500000) / 1000000;
		week = ubx_data_uint16(data, 8);

		if (tow < 0)
		{
		    tow += 604800000;
		    week -= 1;
		}
		
		if (tow >= 604800000)
		{
		    tow -= 604800000;
		    week += 1;
		}

		context->week = week;
		context->tow = tow;
		
		device->location.correction = ubx_data_uint8(data, 10);
	    }
	    else
	    {
		context->week = 0;
		context->tow = 0;

		device->location.correction = 0;
	    }

	    device->seen |= UBX_MESSAGE_MASK_NAV_TIMEGPS;
	    device->seen &= ~UBX_MESSAGE_MASK_SOLUTION;
	    break;

	case 0x30:
	    /* UBX-NAV-SVINFO */

	    device->seen |= UBX_MESSAGE_MASK_NAV_SVINFO;
	    break;

	default:
	    break;
	}
    }

    else if (message == 0x0500)
    {
	/* UBX-ACK-NACK */
	command = ((ubx_data_uint8(data, 0) << 8) | ubx_data_uint8(data, 1));

	if (command == device->command)
	{
	    device->command = ~0l;
	    
	    ubx_configure(device, GNSS_RESPONSE_NACK, command);
	}
    }

    else if (message == 0x0501)
    {
	/* UBX-ACK-ACK */
	command = ((ubx_data_uint8(data, 0) << 8) | ubx_data_uint8(data, 1));

	if (command == device->command)
	{
	    device->command = ~0l;
	    
	    ubx_configure(device, GNSS_RESPONSE_ACK, command);
	}
    }

    if (device->init == GNSS_INIT_DONE)
    {
	expected = device->expected & (UBX_MESSAGE_MASK_NAV_DOP |
				       UBX_MESSAGE_MASK_NAV_PVT |
				       UBX_MESSAGE_MASK_NAV_TIMEGPS);
	
	if ((device->seen & expected) == expected)
	{
	    if (context->week && device->location.time.year)
	    {
		if (!(device->seen & UBX_MESSAGE_MASK_NAV_TIMEGPS))
		{
		    device->location.correction = utc_offset_time(&device->location.time, context->week, context->tow);
		}

		device->location.mask |= (GNSS_LOCATION_MASK_TIME | GNSS_LOCATION_MASK_CORRECTION);
	    }
	    
	    gnss_location(device);
	    
	    device->seen &= ~(UBX_MESSAGE_MASK_NAV_DOP |
			      UBX_MESSAGE_MASK_NAV_PVT |
			      UBX_MESSAGE_MASK_NAV_TIMEGPS);
	    
	    device->seen |= UBX_MESSAGE_MASK_SOLUTION;
	}
	
	expected = device->expected & UBX_MESSAGE_MASK_NAV_SVINFO;
	
	if ((device->seen & UBX_MESSAGE_MASK_SOLUTION) && ((device->seen & expected) == expected))
	{
	    gnss_satellites(device);
	    
	    device->seen &= ~UBX_MESSAGE_MASK_NAV_SVINFO;
	}
    }
}

static const uint8_t ubx_cfg_msg_nav_pvt[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0x01,                                           /* CLASS                     */
    0x07,                                           /* ID                        */
    0x01,                                           /* RATE DDC                  */
    0x01,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x19, 0xe7,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nav_timegps[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0x01,                                           /* CLASS                     */
    0x20,                                           /* ID                        */
    0x01,                                           /* RATE DDC                  */
    0x01,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x32, 0x96,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nav_dop[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0x01,                                           /* CLASS                     */
    0x04,                                           /* ID                        */
    0x01,                                           /* RATE DDC                  */
    0x01,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x16, 0xd2,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nav_svinfo_1hz[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0x01,                                           /* CLASS                     */
    0x30,                                           /* ID                        */
    0x01,                                           /* RATE DDC                  */
    0x01,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x42, 0x06,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nav_svinfo_5hz[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0x01,                                           /* CLASS                     */
    0x30,                                           /* ID                        */
    0x05,                                           /* RATE DDC                  */
    0x05,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x4a, 0x32,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nav_svinfo_10hz[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0x01,                                           /* CLASS                     */
    0x30,                                           /* ID                        */
    0x0a,                                           /* RATE DDC                  */
    0x0a,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x54, 0x69,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nmea_gga[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0xf0,                                           /* CLASS                     */
    0x00,                                           /* ID                        */
    0x00,                                           /* RATE DDC                  */
    0x00,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0xff, 0x23,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nmea_gll[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0xf0,                                           /* CLASS                     */
    0x01,                                           /* ID                        */
    0x00,                                           /* RATE DDC                  */
    0x00,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x00, 0x2a,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nmea_gsa[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0xf0,                                           /* CLASS                     */
    0x02,                                           /* ID                        */
    0x00,                                           /* RATE DDC                  */
    0x00,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x01, 0x31,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nmea_gsv[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0xf0,                                           /* CLASS                     */
    0x03,                                           /* ID                        */
    0x00,                                           /* RATE DDC                  */
    0x00,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x02, 0x38,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nmea_rmc[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0xf0,                                           /* CLASS                     */
    0x04,                                           /* ID                        */
    0x00,                                           /* RATE DDC                  */
    0x00,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x03, 0x3f,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_msg_nmea_vtg[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x01,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0xf0,                                           /* CLASS                     */
    0x05,                                           /* ID                        */
    0x00,                                           /* RATE DDC                  */
    0x00,                                           /* RATE UART1                */
    0x00,                                           /* RATE UART2                */
    0x00,                                           /* RATE USB                  */
    0x00,                                           /* RATE SPI                  */
    0x00,                                           /*                           */
    0x04, 0x46,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_rate_1hz[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x08,                                     /* CLASS, ID                 */
    0x06, 0x00,                                     /* LENGTH                    */
    0xe8, 0x03,                                     /* MEASUREMENT RATE          */
    0x01, 0x00,                                     /* NAVIGATION  RATE          */
    0x01, 0x00,                                     /* TIME REFERENCE            */
    0x01, 0x39,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_rate_5hz[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x08,                                     /* CLASS, ID                 */
    0x06, 0x00,                                     /* LENGTH                    */
    0xc8, 0x00,                                     /* MEASUREMENT RATE          */
    0x01, 0x00,                                     /* NAVIGATION  RATE          */
    0x01, 0x00,                                     /* TIME REFERENCE            */
    0xde, 0x6a,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_rate_10hz[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x08,                                     /* CLASS, ID                 */
    0x06, 0x00,                                     /* LENGTH                    */
    0x64, 0x00,                                     /* MEASUREMENT RATE          */
    0x01, 0x00,                                     /* NAVIGATION  RATE          */
    0x01, 0x00,                                     /* TIME REFERENCE            */
    0x7a, 0x12,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_tp5[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x31,                                     /* CLASS, ID                 */
    0x20, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* TIMEPULSE                 */
    0x00, 0x00, 0x00,                               /*                           */
    0x32, 0x00,                                     /* ANTENNA CABLE DELAY       */
    0x00, 0x00,                                     /* RF GROUP DELAY            */
    0x40, 0x42, 0x0f, 0x00,                         /* PERIOD                    */
    0x40, 0x42, 0x0f, 0x00,                         /* PERIOD LOCKED             */
    0x40, 0x42, 0x0f, 0x00,                         /* PULSE LENGTH              */
    0xa0, 0xbb, 0x0d, 0x00,                         /* PULSE LENGTH LOCKED       */
    0x00, 0x00, 0x00, 0x00,                         /* USER DELAY                */
    0x37, 0x00, 0x00, 0x00,                         /* FLAGS                     */
    0xdb, 0x06,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_pm2[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x3b,                                     /* CLASS, ID                 */
    0x2c, 0x00,                                     /* LENGTH                    */
    0x01,                                           /* VERSION                   */
    0x00,                                           /* RESERVED1                 */
    0x00,                                           /* RESERVED2                 */
    0x00,                                           /* RESERVED3                 */
    0x00, 0x11, 0x02, 0x00,                         /* FLAGS                     */
    0xe8, 0x03, 0x00, 0x00,                         /* UPDATE PERIOD             */
    0x10, 0x27, 0x00, 0x00,                         /* SEARCH PERIOD             */
    0x00, 0x00, 0x00, 0x00,                         /* GRID OFFSET               */
    0x00, 0x00,                                     /* ON TIME                   */
    0x00, 0x00,                                     /* MIN ACQ TIME              */
    0x00, 0x00,                                     /* RESERVED4                 */
    0x00, 0x00,                                     /* RESERVED5                 */
    0x00, 0x00, 0x00, 0x00,                         /* RESERVED6                 */
    0x00, 0x00, 0x00, 0x00,                         /* RESERVED7                 */
    0x00,                                           /* RESERVED8                 */
    0x00,                                           /* RESERVED9                 */
    0x00, 0x00,                                     /* RESERVED10                */
    0x00, 0x00, 0x00, 0x00,                         /* RESERVED11                */
    0xa3, 0xae,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_gnss_gps_enable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x3e,                                     /* CLASS, ID                 */
    0x0c, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* VERSION                   */
    0x00,                                           /* NUM TRACKING CHANNELS HW  */
    0xff,                                           /* NUM TRACKING CHANNELS SW  */
    0x01,                                           /* NUM CONFIG BLOCKS         */
    0x00, 0x08, 0x10, 0x00, 0x01, 0x00, 0x01, 0x00, /* GPS                       */
    0x6a, 0x47,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_gnss_gps_disable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x3e,                                     /* CLASS, ID                 */
    0x0c, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* VERSION                   */
    0x00,                                           /* NUM TRACKING CHANNELS HW  */
    0xff,                                           /* NUM TRACKING CHANNELS SW  */
    0x01,                                           /* NUM CONFIG BLOCKS         */
    0x00, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, /* GPS                       */
    0x68, 0x41,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_gnss_sbas_enable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x3e,                                     /* CLASS, ID                 */
    0x0c, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* VERSION                   */
    0x00,                                           /* NUM TRACKING CHANNELS HW  */
    0xff,                                           /* NUM TRACKING CHANNELS SW  */
    0x01,                                           /* NUM CONFIG BLOCKS         */
    0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00, /* SBAS                      */
    0x57, 0xd0,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_gnss_sbas_disable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x3e,                                     /* CLASS, ID                 */
    0x0c, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* VERSION                   */
    0x00,                                           /* NUM TRACKING CHANNELS HW  */
    0xff,                                           /* NUM TRACKING CHANNELS SW  */
    0x01,                                           /* NUM CONFIG BLOCKS         */
    0x01, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, /* SBAS                      */
    0x55, 0xca,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_gnss_qzss_enable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x3e,                                     /* CLASS, ID                 */
    0x0c, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* VERSION                   */
    0x00,                                           /* NUM TRACKING CHANNELS HW  */
    0xff,                                           /* NUM TRACKING CHANNELS SW  */
    0x01,                                           /* NUM CONFIG BLOCKS         */
    0x05, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00, /* QZSS                      */
    0x5a, 0xe9,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_gnss_qzss_disable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x3e,                                     /* CLASS, ID                 */
    0x0c, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* VERSION                   */
    0x00,                                           /* NUM TRACKING CHANNELS HW  */
    0xff,                                           /* NUM TRACKING CHANNELS SW  */
    0x01,                                           /* NUM CONFIG BLOCKS         */
    0x05, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, /* QZSS                      */
    0x58, 0xe3,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_gnss_glonass_enable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x3e,                                     /* CLASS, ID                 */
    0x0c, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* VERSION                   */
    0x00,                                           /* NUM TRACKING CHANNELS HW  */
    0xff,                                           /* NUM TRACKING CHANNELS SW  */
    0x01,                                           /* NUM CONFIG BLOCKS         */
    0x06, 0x08, 0x0e, 0x00, 0x01, 0x00, 0x01, 0x00, /* GLONASS                   */
    0x6e, 0x6b,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_gnss_glonass_disable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x3e,                                     /* CLASS, ID                 */
    0x0c, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* VERSION                   */
    0x00,                                           /* NUM TRACKING CHANNELS HW  */
    0xff,                                           /* NUM TRACKING CHANNELS SW  */
    0x01,                                           /* NUM CONFIG BLOCKS         */
    0x06, 0x08, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, /* GLONASS                   */
    0x6c, 0x65,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_sbas_disable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x16,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* MODE                      */
    0x00,                                           /* USAGE                     */
    0x00,                                           /* MAX SBAS                  */
    0x00,                                           /* SCANMODE2                 */
    0x00, 0x00, 0x00, 0x00,                         /* SCANMODE1 (133, 135, 138) */
    0x24, 0x8a,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_sbas_auto[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x16,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0x01,                                           /* MODE                      */
    0x03,                                           /* USAGE                     */
    0x03,                                           /* MAX SBAS                  */
    0x00,                                           /* SCANMODE2                 */
    0x89, 0xa3, 0x07, 0x00,                         /* SCANMODE1                 */
    0x5e, 0xd4,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_rxm_continuous[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x11,                                     /* CLASS, ID                 */
    0x02, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* RESERVED                  */
    0x00,                                           /* MODE                      */
    0x19, 0x81,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_rxm_powersave[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x11,                                     /* CLASS, ID                 */
    0x02, 0x00,                                     /* LENGTH                    */
    0x00,                                           /* RESERVED                  */
    0x01,                                           /* MODE                      */
    0x1a, 0x82,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_save[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x09,                                     /* CLASS, ID                 */
    0x0d, 0x00,                                     /* LENGTH                    */
    0x00, 0x00, 0x00, 0x00,                         /* CLEAR MASK                */
    0xff, 0xff, 0xff, 0xff,                         /* SAVE MASK                 */
    0x00, 0x00, 0x00, 0x00,                         /* LOAD MASK                 */
    0x01,                                           /* DEVICE MASK               */
    0x19, 0x9c,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_rxm_pmreq[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x02, 0x41,                                     /* CLASS, ID                 */
    0x08, 0x00,                                     /* LENGTH                    */
    0x00, 0x00, 0x00, 0x00,                         /* DURATION                  */
    0x02, 0x00, 0x00, 0x00,                         /* FLAGS                     */
    0x4d, 0x3b,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_external_enable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x13,                                     /* CLASS, ID                 */
    0x04, 0x00,                                     /* LENGTH                    */
    0x00, 0x00,                                     /* FLAGS                     */
    0xf0, 0xb9,                                     /* PINS                      */
    0xc6, 0x66,                                     /* CK_A, CK_B                */
};

static const uint8_t ubx_cfg_external_disable[] = {
    0xb5, 0x62,                                     /* SYNC_CHAR_1, SYNC_CHAR_2  */
    0x06, 0x13,                                     /* CLASS, ID                 */
    0x04, 0x00,                                     /* LENGTH                    */
    0x01, 0x00,                                     /* FLAGS                     */
    0xf0, 0xb9,                                     /* PINS                      */
    0xc7, 0x6a,                                     /* CK_A, CK_B                */
};

static const uint8_t * const ubx_init_table_1hz[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_msg_nav_pvt,
    ubx_cfg_msg_nav_timegps,
    ubx_cfg_msg_nav_dop,
    ubx_cfg_msg_nav_svinfo_1hz,
    ubx_cfg_msg_nmea_gga,
    ubx_cfg_msg_nmea_gll,
    ubx_cfg_msg_nmea_gsa,
    ubx_cfg_msg_nmea_gsv,
    ubx_cfg_msg_nmea_rmc,
    ubx_cfg_msg_nmea_vtg,
    ubx_cfg_rate_1hz,
    ubx_cfg_tp5,
    ubx_cfg_gnss_glonass_enable,
    ubx_cfg_gnss_sbas_enable,
    ubx_cfg_gnss_qzss_disable,
    ubx_cfg_sbas_auto,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_init_table_5hz[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_msg_nav_pvt,
    ubx_cfg_msg_nav_timegps,
    ubx_cfg_msg_nav_dop,
    ubx_cfg_msg_nav_svinfo_5hz,
    ubx_cfg_msg_nmea_gga,
    ubx_cfg_msg_nmea_gll,
    ubx_cfg_msg_nmea_gsa,
    ubx_cfg_msg_nmea_gsv,
    ubx_cfg_msg_nmea_rmc,
    ubx_cfg_msg_nmea_vtg,
    ubx_cfg_rate_5hz,
    ubx_cfg_tp5,
    ubx_cfg_gnss_glonass_enable,
    ubx_cfg_gnss_sbas_enable,
    ubx_cfg_gnss_qzss_disable,
    ubx_cfg_sbas_auto,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_init_table_10hz[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_msg_nav_pvt,
    ubx_cfg_msg_nav_timegps,
    ubx_cfg_msg_nav_dop,
    ubx_cfg_msg_nav_svinfo_10hz,
    ubx_cfg_msg_nmea_gga,
    ubx_cfg_msg_nmea_gll,
    ubx_cfg_msg_nmea_gsa,
    ubx_cfg_msg_nmea_gsv,
    ubx_cfg_msg_nmea_rmc,
    ubx_cfg_msg_nmea_vtg,
    ubx_cfg_rate_10hz,
    ubx_cfg_tp5,
    ubx_cfg_gnss_glonass_enable,
    ubx_cfg_gnss_sbas_enable,
    ubx_cfg_gnss_qzss_disable,
    ubx_cfg_sbas_auto,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_external_enable_table[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_external_enable,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_external_disable_table[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_external_disable,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_constellation_gps_table[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_gnss_glonass_disable,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_constellation_gps_glonass_table[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_gnss_glonass_enable,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_sbas_enable_table[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_gnss_sbas_enable,
    ubx_cfg_sbas_auto,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_sbas_disable_table[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_gnss_sbas_disable,
    ubx_cfg_sbas_disable,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_qzss_enable_table[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_gnss_qzss_enable,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_qzss_disable_table[] = {
    ubx_cfg_rxm_continuous,
    ubx_cfg_pm2,
    ubx_cfg_gnss_qzss_disable,
    ubx_cfg_save,
    NULL,
};

static const uint8_t * const ubx_wakeup_table[] = {
    ubx_cfg_rxm_continuous,
    NULL,
};


static void ubx_checksum(gnss_device_t *device, uint8_t *data)
{
    uint8_t ck_a, ck_b;
    unsigned int i, count;

    count = (data[4] | (data[5] << 8)) + 8;

    ck_a = 0;
    ck_b = 0;
    
    for (i = 2; i < (count -2); i++)
    {
	ck_a += data[i];
	ck_b += ck_a;
    }
	
    data[count -2] = ck_a;
    data[count -1] = ck_b;
}

static void ubx_send(gnss_device_t *device, const uint8_t *data)
{
    uint32_t command;
    unsigned int count;

    if (data == ubx_cfg_rxm_continuous)
    {
	command = 0x0611;
	count   = sizeof(ubx_cfg_rxm_continuous);
    }
    else
    {
	command = (data[2] << 8) | data[3];
	count  = (data[4] | (data[5] << 8)) + 8;
    }

    device->command = command;
    device->busy = 1;

    (*device->send_routine)(device->context, data, count, gnss_send_callback);
}

static void ubx_table(gnss_device_t *device, const uint8_t * const * table)
{
    const uint8_t *data;

    data = table[0];
    device->table = table + 1;

    ubx_send(device, data);
}

static void ubx_configure(gnss_device_t *device, unsigned int response, uint32_t command)
{
    const uint8_t *data = NULL;

    stm32l0_rtc_timer_stop(&device->ubx.timeout);

    if (device->table)
    {
	if (device->init == GNSS_INIT_UBX_BAUD_RATE)
	{
	    device->init = GNSS_INIT_UBX_INIT_TABLE;

	    data = device->table[0];
	    device->table = device->table + 1;
	}
	else
	{
	    if (device->table[0] != NULL)
	    {
		data = device->table[0];
		device->table = device->table + 1;
	    }
	    else
	    {
		device->table = NULL;

		if (device->init == GNSS_INIT_UBX_INIT_TABLE)
		{
		    device->init = GNSS_INIT_DONE;

		    device->expected = (UBX_MESSAGE_MASK_NAV_DOP |
					UBX_MESSAGE_MASK_NAV_PVT |
					UBX_MESSAGE_MASK_NAV_SVINFO |
					UBX_MESSAGE_MASK_NAV_TIMEGPS);
		
		    device->seen = 0;
		    device->location.type = 0;
		    device->location.mask = 0;
		}
	    }
	}
    }

    if (data)
    {
	ubx_send(device, data);

	stm32l0_rtc_timer_start(&device->ubx.timeout, 0, 8192, false); // 250ms

    }
}

static void ubx_timeout(gnss_device_t *device)
{
    const uint8_t *data = NULL;

    if (device->table)
    {
	data = *(device->table -1);

	ubx_send(device, data);

	stm32l0_rtc_timer_start(&device->ubx.timeout, 0, 8192, false); // 250ms
    }
}

/************************************************************************************/

static void gnss_send_callback(void)
{
    gnss_device_t *device = &gnss_device;

    device->busy = 0;
}

void gnss_receive(const uint8_t *data, uint32_t count)
{
    gnss_device_t *device = &gnss_device;
    uint8_t c;

    while (count > 0)
    {
	count--;

	c = *data++;

	if ((device->state <= GNSS_STATE_NMEA_END_LF) && (c == '$'))
	{
	    /* Whenever we see a '$', it's the start of a new sentence,
	     * which can discard a partially read one.
	     */
	    
	    device->state = GNSS_STATE_NMEA_PAYLOAD;
	    device->checksum = 0x00;
	    device->rx_count = 0;

	    nmea_start_sentence(device);
	}
	else
	{
	    switch (device->state) {
	    case GNSS_STATE_START:
		if ((device->mode == GNSS_MODE_UBLOX) && (c == 0xb5))
		{
		    device->state = GNSS_STATE_UBX_SYNC_2;
		}
		break;

	    case GNSS_STATE_NMEA_PAYLOAD:
		if (c == '*')
		{
		    device->rx_data[device->rx_count] = '\0';

		    nmea_parse_sentence(device, &device->rx_data[0], device->rx_count);

		    device->state = GNSS_STATE_NMEA_CHECKSUM_1;
		}
		else if ((c >= 0x20) && (c <= 0x7f))
		{
		    if (device->rx_count >= GNSS_RX_DATA_SIZE)
		    {
			/* Reject a too long sentence.
			 */
			device->state = GNSS_STATE_START;
		    }
		    else
		    {
			device->checksum ^= c;

			if (c == ',')
			{
			    device->rx_data[device->rx_count] = '\0';
			    
			    nmea_parse_sentence(device, &device->rx_data[0], device->rx_count);

			    device->rx_count = 0;
			}
			else
			{
			    device->rx_data[device->rx_count++] = c;
			}
		    }
		}
		else
		{
		    /* If there is an illegal char, then scan again for a new start.
		     */
		    device->state = GNSS_STATE_START;
		}
		break;

	    case GNSS_STATE_NMEA_CHECKSUM_1:
		if (c == nmea_hex_ascii[device->checksum >> 4])
		{
		    device->state = GNSS_STATE_NMEA_CHECKSUM_2;
		}
		else
		{
		    /* If there is a checksum error, then scan again for a new start.
		     */

		    device->state = GNSS_STATE_START;
		}
		break;

	    case GNSS_STATE_NMEA_CHECKSUM_2:
		if (c == nmea_hex_ascii[device->checksum & 0x0f])
		{
		    device->state = GNSS_STATE_NMEA_END_CR;
		}
		else
		{
		    /* If there is a checksum error, then scan again for a new start.
		     */

		    device->state = GNSS_STATE_START;
		}
		break;

	    case GNSS_STATE_NMEA_END_CR:
		if (c == '\r')
		{
		    device->state = GNSS_STATE_NMEA_END_LF;
		}
		else
		{
		    /* If there is an illegal char, then scan again for a new start.
		     */

		    device->state = GNSS_STATE_START;
		}
		break;

	    case GNSS_STATE_NMEA_END_LF:
		if (c == '\n')
		{
		    if (device->init != GNSS_INIT_DONE)
		    {
			if (device->init == GNSS_INIT_MTK_BAUD_RATE)
			{
			    mtk_configure(device, GNSS_RESPONSE_NMEA_SENTENCE, ~0l);
			}

			if (device->init == GNSS_INIT_UBX_BAUD_RATE)
			{
			    ubx_configure(device, GNSS_RESPONSE_NMEA_SENTENCE, ~0l);
			}
		    }

		    nmea_end_sentence(device);
		}
		 
		device->state = GNSS_STATE_START;
		break;

	    case GNSS_STATE_UBX_SYNC_2:
		if (c != 0x62)
		{
		    device->state = GNSS_STATE_START;
		}		
		else
		{
		    device->state = GNSS_STATE_UBX_MESSAGE_1;
		}
		break;
		
	    case GNSS_STATE_UBX_MESSAGE_1:
		device->ubx.ck_a = c;
		device->ubx.ck_b = c;
		device->ubx.message = (c << 8);
		device->state = GNSS_STATE_UBX_MESSAGE_2;
		break;
		
	    case GNSS_STATE_UBX_MESSAGE_2:
		device->ubx.ck_a += c;
		device->ubx.ck_b += device->ubx.ck_a;
		device->ubx.message |= c;
		device->state = GNSS_STATE_UBX_LENGTH_1;
		break;

	    case GNSS_STATE_UBX_LENGTH_1:
		device->ubx.ck_a += c;
		device->ubx.ck_b += device->ubx.ck_a;
		device->ubx.length = c;
		device->state = GNSS_STATE_UBX_LENGTH_2;
		break;
		
	    case GNSS_STATE_UBX_LENGTH_2:
		device->ubx.ck_a += c;
		device->ubx.ck_b += device->ubx.ck_a;
		device->rx_count = 0;
		device->rx_offset = 0;
		device->rx_chunk = ~0l;
		device->ubx.length |= (c << 8);

		ubx_start_message(device, device->ubx.message, device->ubx.length);

		if (device->rx_count == device->ubx.length)
		{
		    device->state = GNSS_STATE_UBX_CK_A;
		}
		else
		{
		    device->state = GNSS_STATE_UBX_PAYLOAD;
		}
		break;
	    
	    case GNSS_STATE_UBX_PAYLOAD:
		device->ubx.ck_a += c;
		device->ubx.ck_b += device->ubx.ck_a;

		if ((device->rx_count - device->rx_offset) < GNSS_RX_DATA_SIZE)
		{
		    device->rx_data[device->rx_count - device->rx_offset] = c;
		}
		
		device->rx_count++;
		
		if (device->rx_count == device->rx_chunk)
		{
		    ubx_parse_message(device, device->ubx.message, &device->rx_data[0], device->rx_count);
		}

		if (device->rx_count == device->ubx.length)
		{
		    device->state = GNSS_STATE_UBX_CK_A;
		}
		break;

	    case GNSS_STATE_UBX_CK_A:
		device->ubx.ck_a ^= c;
		device->state = GNSS_STATE_UBX_CK_B;
		break;
		
	    case GNSS_STATE_UBX_CK_B:
		device->ubx.ck_b ^= c;

		if ((device->ubx.ck_a == 0x00) &&  (device->ubx.ck_b == 0x00))
		{
		    if (device->init != GNSS_INIT_DONE)
		    {
			if (device->init == GNSS_INIT_UBX_BAUD_RATE)
			{
			    ubx_configure(device, GNSS_RESPONSE_UBX_MESSAGE, ~0l);
			}
		    }

		    if ((device->rx_count - device->rx_offset) <= GNSS_RX_DATA_SIZE)
		    {
			ubx_end_message(device, device->ubx.message, &device->rx_data[0], device->rx_count);
		    }
		}

		device->state = GNSS_STATE_START;
		break;

	    default:
		break;
	    }
	}
    }
}

void gnss_initialize(unsigned int mode, unsigned int rate, unsigned int speed, gnss_send_routine_t send_routine, gnss_location_callback_t location_callback, gnss_satellites_callback_t satellites_callback, void *context)
{
    gnss_device_t *device = &gnss_device;
    const char *uart_data = NULL;
    unsigned int uart_count = 0;

    device->send_routine = send_routine;
    device->location_callback = location_callback;
    device->satellites_callback = satellites_callback;
    device->context = context;

    device->state = GNSS_STATE_START;
    device->command = -1;
    device->busy = 0;

    memset(&device->location, 0, sizeof(device->location));
    memset(&device->satellites, 0, sizeof(device->satellites));

    if (mode == GNSS_MODE_UBLOX)
    {
	device->mode = mode;
	device->init = GNSS_INIT_UBX_BAUD_RATE;

	if (rate >= 10)
	{
	    device->table = ubx_init_table_10hz;
	}
	else if (rate >= 5)
	{
	    device->table = ubx_init_table_5hz;
	}
	else
	{
	    device->table = ubx_init_table_1hz;
	}

	if (speed >= 115200)
	{
	    uart_data = "$PUBX,41,1,0007,0003,115200,0*18\r\n";
	}
	else if (speed >= 57600)
	{
	    uart_data = "$PUBX,41,1,0007,0003,57600,0*2B\r\n";
	}
	else if (speed >= 38400)
	{
	    uart_data = "$PUBX,41,1,0007,0003,38400,0*20\r\n";
	}
	else if (speed >= 19200)
	{
	    uart_data = "$PUBX,41,1,0007,0003,19200,0*25\r\n";
	}
	else
	{
	    uart_data = "$PUBX,41,1,0007,0003,9600,0*10\r\n";
	}
	
	uart_count = strlen(uart_data);

	stm32l0_rtc_timer_create(&device->ubx.timeout, (stm32l0_rtc_callback_t)&ubx_timeout, device);
    }
    else
    {
	if (mode == GNSS_MODE_MEDIATEK)
	{
	    device->mode = mode;
	    device->init = GNSS_INIT_MTK_BAUD_RATE;

	    if (rate >= 5)
	    {
		device->table = mtk_init_table_5hz;
	    }
	    else
	    {
		device->table = mtk_init_table_1hz;
	    }
	
	    if (speed >= 115200)
	    {
		uart_data = "$PMTK251,115200*1F\r\n";
	    }
	    else if (speed >= 57600)
	    {
		uart_data = "$PMTK251,57600*2C\r\n";
	    }
	    else if (speed >= 38400)
	    {
		uart_data = "$PMTK251,38400*27\r\n";
	    }
	    else if (speed >= 19200)
	    {
		uart_data = "$PMTK251,19200*22\r\n";
	    }
	    else
	    {
		uart_data = "$PMTK251,9600*17\r\n";
	    }

	    uart_count = strlen(uart_data);
	}
	else
	{
	    device->mode = GNSS_MODE_NMEA;
	    device->init = GNSS_INIT_DONE;
	    device->table = NULL;
	    device->expected = NMEA_SENTENCE_MASK_GPGGA | NMEA_SENTENCE_MASK_GPGSA | NMEA_SENTENCE_MASK_GPGSV | NMEA_SENTENCE_MASK_GPRMC;
	}
    }

    if (uart_data)
    {
	(device->send_routine)(device->context, (const uint8_t*)uart_data, uart_count, NULL);
    }
}

bool gnss_set_external(bool on)
{
    gnss_device_t *device = &gnss_device;

    if (!gnss_done())
    {
	return false;
    }

    switch (device->mode) {
    case GNSS_MODE_NMEA:
	break;
    case GNSS_MODE_MEDIATEK:
	break;
    case GNSS_MODE_UBLOX:
	ubx_table(device, (on ? ubx_external_enable_table : ubx_external_disable_table));
	break;
    }

    return true;
}

bool gnss_set_constellation(unsigned int mask)
{
    gnss_device_t *device = &gnss_device;

    if (!gnss_done())
    {
	return false;
    }

    switch (device->mode) {
    case GNSS_MODE_NMEA:
	break;
    case GNSS_MODE_MEDIATEK:
	mtk_table(device, ((mask & GNSS_CONSTELLATION_GLONASS) ? mtk_constellation_gps_glonass_table : mtk_constellation_gps_table));
	break;
    case GNSS_MODE_UBLOX:
	ubx_table(device, ((mask & GNSS_CONSTELLATION_GLONASS) ? ubx_constellation_gps_glonass_table : ubx_constellation_gps_table));
	break;
    }

    return true;
}

bool gnss_set_sbas(bool on)
{
    gnss_device_t *device = &gnss_device;

    if (!gnss_done())
    {
	return false;
    }

    switch (device->mode) {
    case GNSS_MODE_NMEA:
	break;
    case GNSS_MODE_MEDIATEK:
	mtk_table(device, (on ? mtk_sbas_enable_table : mtk_sbas_disable_table));
	break;
    case GNSS_MODE_UBLOX:
	ubx_table(device, (on ? ubx_sbas_enable_table : ubx_sbas_disable_table));
	break;
    }

    return true;
}

bool gnss_set_qzss(bool on)
{
    gnss_device_t *device = &gnss_device;

    if (!gnss_done())
    {
	return false;
    }

    switch (device->mode) {
    case GNSS_MODE_NMEA:
	break;
    case GNSS_MODE_MEDIATEK:
	mtk_table(device, (on ? mtk_qzss_enable_table : mtk_qzss_disable_table));
	break;
    case GNSS_MODE_UBLOX:
	ubx_table(device, (on ? ubx_qzss_enable_table : ubx_qzss_disable_table));
	break;
    }

    return true;
}

bool gnss_set_periodic(unsigned int onTime, unsigned int period, bool force)
{
    gnss_device_t *device = &gnss_device;
    const uint8_t **table;
    uint8_t *data;
    unsigned int updatePeriod, searchPeriod;

    if (!gnss_done())
    {
	return false;
    }

    switch (device->mode) {
    case GNSS_MODE_NMEA:
    case GNSS_MODE_MEDIATEK:
	break;
    case GNSS_MODE_UBLOX:
	if (onTime == 0)
	{
	    updatePeriod = 1000;
	    searchPeriod = 10000;
	}
	else
	{
	    updatePeriod = period * 1000;
	    searchPeriod = period * 1000;
	}

	data = &device->tx_data[0];
	table = &device->tx_table[0];

	memset(&data[0], 0, sizeof(GNSS_TX_DATA_SIZE));
	
	data[ 0] = 0xb5;
	data[ 1] = 0x62;
	data[ 2] = 0x06;
	data[ 3] = 0x3b;
	data[ 4] = 0x2c;
	data[ 5] = 0x00;
	data[ 6] = 0x01;
	data[10] = 0x00;
	data[11] = 0x01;
	data[12] = ((onTime && (updatePeriod >= 10000)) ? (force ? 0x01 : 0x00) : 0x02);
	data[13] = 0x00;
	data[14] = updatePeriod >>  0;
	data[15] = updatePeriod >>  8;
	data[16] = updatePeriod >> 16;
	data[17] = updatePeriod >> 24;
	data[18] = searchPeriod >>  0;
	data[19] = searchPeriod >>  8;
	data[20] = searchPeriod >> 16;
	data[21] = searchPeriod >> 24;
	data[26] = onTime >> 0;
	data[27] = onTime >> 8;
	
	ubx_checksum(device, data);

	table[0] = ubx_cfg_rxm_continuous;
	table[1] = &data[0];

	if (onTime)
	{
	    table[2] = ubx_cfg_rxm_powersave;
	    table[3] = ubx_cfg_save;
	    table[4] = NULL;
	}
	else
	{
	    table[2] = ubx_cfg_save;
	    table[3] = NULL;
	}

	device->table = table + 1;

	ubx_send(device, table[0]);
    }

    return true;
}

bool gnss_sleep(void)
{
    gnss_device_t *device = &gnss_device;

    if (!gnss_done())
    {
	return false;
    }

    switch (device->mode) {
    case GNSS_MODE_NMEA:
    case GNSS_MODE_MEDIATEK:
	break;
    case GNSS_MODE_UBLOX:
	ubx_send(device, ubx_rxm_pmreq);
	break;
    }

    return true;
}

bool gnss_wakeup(void)
{
    gnss_device_t *device = &gnss_device;

    if (!gnss_done())
    {
	return false;
    }

    switch (device->mode) {
    case GNSS_MODE_NMEA:
    case GNSS_MODE_MEDIATEK:
	break;
    case GNSS_MODE_UBLOX:
	ubx_send(device, ubx_cfg_rxm_continuous);
	break;
    }

    return true;
}

bool gnss_done(void)
{
    gnss_device_t *device = &gnss_device;

    if (device->table != NULL)
    {
	return false;
    }

    if (device->busy)
    {
	return false;
    }

    return true;
}
