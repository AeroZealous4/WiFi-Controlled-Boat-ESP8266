/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2015 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

// Project Name:Twin Motor Powered RC Boat
// Software Ver:V0.0
// Author:      Akshay Pampatwar 
// Description: ESP8266 is interfaced with driver for controlling speed of two brushed motors.
//              Differential action of two motors are used for controlling the boat.
//              TCP Ip packet is received from mobile application (In this project android application Universal RC transmitter is used). 
//              Corresponding duty cycle for two motors are then calculated. From these calculated duty cycle values 
//              PWM signal is applied to motors. Following signal is accepted which is converted into PWM.
//              ch[0]: Fwd Speed (ch[0]=127 : 0 speed)
//              ch[1]: Direction of rudder (ch[1]=127 : Neutral pos of rudder i.e. boat will go straight)


#include "ets_sys.h"
#include "osapi.h"
#include "spi_test.h"
#include "user_interface.h"
#include "driver/uart.h"
#include "driver/hw_timer.h"
#include "eagle_soc.h"
#include "gpio.h"
#include "pwm.h"
#include "espconn.h"
#include "mem.h"

LOCAL struct espconn esp_conn;
LOCAL esp_tcp esptcp;

uint32 duty[] = {16000,16000};
char ch[]={127,127,0,0,0,0,0,0,0,0};  //ch1 to ch10, Seq: LV,LH,RV,RH,..,CMD
int32 MT_PWM[]={0,0};                 //For RC boat two channels  Max Period * 1000 /45 (Ref datasheet for More info)
uint8 MT_Dir[]={1,1};                 //1: Forward, 0: Backwards.
uint32 PWM_Period = 1000;             //uSecs should not be less than 20uS  

#define Max_Ch_No 2                   //
#define SERVER_LOCAL_PORT   1112      //Port no. to which app should be connected.

#define PWM_0_OUT_IO_MUX PERIPHS_IO_MUX_GPIO0_U
#define PWM_0_OUT_IO_NUM 0
#define PWM_0_OUT_IO_FUNC FUNC_GPIO0

#define PWM_1_OUT_IO_MUX PERIPHS_IO_MUX_GPIO2_U
#define PWM_1_OUT_IO_NUM 2
#define PWM_1_OUT_IO_FUNC FUNC_GPIO2

#if ((SPI_FLASH_SIZE_MAP == 0) || (SPI_FLASH_SIZE_MAP == 1))
#error "The flash map is not supported"
#elif (SPI_FLASH_SIZE_MAP == 2)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA_2_ADDR							0x81000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0xfb000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0xfc000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0xfd000
#elif (SPI_FLASH_SIZE_MAP == 3)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA_2_ADDR							0x81000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0x1fb000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0x1fc000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0x1fd000
#elif (SPI_FLASH_SIZE_MAP == 4)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA_2_ADDR							0x81000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0x3fb000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0x3fc000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0x3fd000
#elif (SPI_FLASH_SIZE_MAP == 5)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA_2_ADDR							0x101000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0x1fb000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0x1fc000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0x1fd000
#elif (SPI_FLASH_SIZE_MAP == 6)
#define SYSTEM_PARTITION_OTA_SIZE							0x6A000
#define SYSTEM_PARTITION_OTA_2_ADDR							0x101000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0x3fb000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0x3fc000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0x3fd000
#else
#error "The flash map is not supported"
#endif

static const partition_item_t at_partition_table[] = {
    { SYSTEM_PARTITION_BOOTLOADER, 						0x0, 												0x1000},
    { SYSTEM_PARTITION_OTA_1,   						0x1000, 											SYSTEM_PARTITION_OTA_SIZE},
    { SYSTEM_PARTITION_OTA_2,   						SYSTEM_PARTITION_OTA_2_ADDR, 						SYSTEM_PARTITION_OTA_SIZE},
    { SYSTEM_PARTITION_RF_CAL,  						SYSTEM_PARTITION_RF_CAL_ADDR, 						0x1000},
    { SYSTEM_PARTITION_PHY_DATA, 						SYSTEM_PARTITION_PHY_DATA_ADDR, 					0x1000},
    { SYSTEM_PARTITION_SYSTEM_PARAMETER, 				SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR, 			0x3000},
};

void ICACHE_FLASH_ATTR user_pre_init(void)
{
    if(!system_partition_table_regist(at_partition_table, sizeof(at_partition_table)/sizeof(at_partition_table[0]),SPI_FLASH_SIZE_MAP)) {
		os_printf("system_partition_table_regist fail\r\n");
		while(1);
	}
}


//Software timer
static os_timer_t sw_timer;
int i=0;
// os_timer_func_t sw_timer_cb(void *arg);

void ICACHE_FLASH_ATTR
sw_timer_cb(void *arg)
{
    // os_printf("\n\rThis is a software timer subroutine\n\r");

    // MT_PWM[0] = ch[0];
    // MT_PWM[1] = ch[1];

    //Demux 
    MT_PWM[0] = ch[0] + ch[1] - 254;
    MT_PWM[1] = ch[0] - ch[1];

    if(MT_PWM[0]>127)
        MT_PWM[0] = 127;
    else if(MT_PWM[0] < -127)
        MT_PWM[0] = -127;

    if(MT_PWM[1]>127)
        MT_PWM[1] = 127;
    else if(MT_PWM[1] < -127)
        MT_PWM[1] = -127;

    // MT_PWM[0] -= 127;
    // MT_PWM[1] -= 127;

    if(MT_PWM[0] >= 0)
    {
        // duty[0] = (MT_PWM[0] - 127)*175;
        // duty[0] *= 175;
        MT_Dir[0] = 1;
    }
    else
    {
        // duty[0] = (MT_PWM[0])*175;
        MT_PWM[0] = - MT_PWM[0];
        MT_Dir[0] = 0;
    }
       
    duty[0] = (MT_PWM[0])*175; 


    if(MT_PWM[1] >= 0)
        {
            // duty[1] = (MT_PWM[1] - 127)*175;
            MT_Dir[1] = 1;
        }
    else
        {
            // duty[1] = (MT_PWM[1])*175;
            MT_PWM[1] = - MT_PWM[1];
            MT_Dir[1] = 0;
        }

    duty[1] = (MT_PWM[1])*175;

    // MT_PWM[0] +=2;
    // MT_PWM[1] +=2;
    
    // if(MT_PWM[0] > 253)
    // {
    //     MT_PWM[0] = 0;
    //     MT_PWM[1] = 0;
    // }
  

    pwm_set_duty(duty[0],0);
    pwm_set_duty(duty[1],1);
    // os_printf("\n\rDuty %d ,Duty var %d \n\r",pwm_get_duty(0),duty[0]);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(1),MT_Dir[0]);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(3),MT_Dir[1]);
    pwm_start();
    
    if(i == 0)
        {
            i=1;
            GPIO_OUTPUT_SET(GPIO_ID_PIN(5),1);
        }
    else
    {
        i=0;
        GPIO_OUTPUT_SET(GPIO_ID_PIN(5),0);
    }
}




// void   hw_test_timer_cb(void)
// {
    
//     os_printf("\n\rThis is a hardware timer subroutine\n\r");
//     //hw_timer_arm(50);
//     if(i == 0)
//         {
//             i=1;
//             GPIO_OUTPUT_SET(GPIO_ID_PIN(4),1);
//         }
//     else
//     {
//         i=0;
//         GPIO_OUTPUT_SET(GPIO_ID_PIN(4),0);
//     }
// }

 
/******************************************************************************
 * FunctionName : tcp_server_sent_cb
 * Description  : data sent callback.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
tcp_server_sent_cb(void *arg)
{
   //data sent successfully
 
    // os_printf("tcp sent cb \r\n");
}
 
 
/******************************************************************************
 * FunctionName : tcp_server_recv_cb
 * Description  : receive callback.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
tcp_server_recv_cb(void *arg, char *pusrdata, unsigned short length)
{
   //received some data from tcp connection

    uint8 i = 0;
   struct espconn *pespconn = arg;
   // os_printf("tcp recv : %s \r\n", pusrdata);
   
   //if(length == Max_Ch_No)
       for (i = 0; i < Max_Ch_No + 1; i ++)
       {
            ch[i]=pusrdata[i];
           // MT_PWM[i] = (char)((pusrdata[i] - 127)*0.757401574);
       }
   
    // os_printf("channels :  %c ,  %c \r\n", ch[0],ch[1]);
    //os_printf("MT_PWM : %c , %c \r\n", MT_PWM[0],MT_PWM[1]);
    
   // espconn_sent(pespconn, pusrdata, length);
    
}
 
/******************************************************************************
 * FunctionName : tcp_server_discon_cb
 * Description  : disconnect callback.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
tcp_server_discon_cb(void *arg)
{
   //tcp disconnect successfully
    
    // os_printf("tcp disconnect succeed !!! \r\n");
}
 
/******************************************************************************
 * FunctionName : tcp_server_recon_cb
 * Description  : reconnect callback, error occured in TCP connection.
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
tcp_server_recon_cb(void *arg, sint8 err)
{
   //error occured , tcp connection broke. 
    
    // os_printf("reconnect callback, error code %d !!! \r\n",err);
}
 
LOCAL void tcp_server_multi_send(void)
{
   struct espconn *pesp_conn = &esp_conn;
 
   remot_info *premot = NULL;
   uint8 count = 0;
   sint8 value = ESPCONN_OK;
   if (espconn_get_connection_info(pesp_conn,&premot,0) == ESPCONN_OK){
      char *pbuf = "tcp_server_multi_send\n";
      for (count = 0; count < pesp_conn->link_cnt; count ++){
         pesp_conn->proto.tcp->remote_port = premot[count].remote_port;
          
         pesp_conn->proto.tcp->remote_ip[0] = premot[count].remote_ip[0];
         pesp_conn->proto.tcp->remote_ip[1] = premot[count].remote_ip[1];
         pesp_conn->proto.tcp->remote_ip[2] = premot[count].remote_ip[2];
         pesp_conn->proto.tcp->remote_ip[3] = premot[count].remote_ip[3];
 
         espconn_sent(pesp_conn, pbuf, os_strlen(pbuf));
      }
   }
}
 
 
/******************************************************************************
 * FunctionName : tcp_server_listen
 * Description  : TCP server listened a connection successfully
 * Parameters   : arg -- Additional argument to pass to the callback function
 * Returns      : none
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
tcp_server_listen(void *arg)
{
    struct espconn *pesp_conn = arg;
    os_printf("tcp_server_listen !!! \r\n");
 
    espconn_regist_recvcb(pesp_conn, tcp_server_recv_cb);
    espconn_regist_reconcb(pesp_conn, tcp_server_recon_cb);
    espconn_regist_disconcb(pesp_conn, tcp_server_discon_cb);
     
    espconn_regist_sentcb(pesp_conn, tcp_server_sent_cb);
   tcp_server_multi_send();
}
 
/******************************************************************************
 * FunctionName : user_tcpserver_init
 * Description  : parameter initialize as a TCP server
 * Parameters   : port -- server port
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_tcpserver_init(uint32 port)
{
    esp_conn.type = ESPCONN_TCP;
    esp_conn.state = ESPCONN_NONE;
    esp_conn.proto.tcp = &esptcp;
    esp_conn.proto.tcp->local_port = port;
    espconn_regist_connectcb(&esp_conn, tcp_server_listen);
 
    sint8 ret = espconn_accept(&esp_conn);
     
    // os_printf("espconn_accept [%d] !!! \r\n", ret);
 
}
LOCAL os_timer_t test_timer;
 
/******************************************************************************
 * FunctionName : user_esp_platform_check_ip
 * Description  : check whether get ip addr or not
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_esp_platform_check_ip(void)
{
    struct ip_info ipconfig;
 
   //disarm timer first
    os_timer_disarm(&test_timer);
 
   //get ip info of ESP8266 station
    wifi_get_ip_info(STATION_IF, &ipconfig);
 
    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0) {
 
      // os_printf("got ip !!! \r\n");
      user_tcpserver_init(SERVER_LOCAL_PORT);
 
    } else {
        
        if ((wifi_station_get_connect_status() == STATION_WRONG_PASSWORD ||
                wifi_station_get_connect_status() == STATION_NO_AP_FOUND ||
                wifi_station_get_connect_status() == STATION_CONNECT_FAIL)) {
                 
         os_printf("connect fail !!! \r\n");
          
        } else {
         
           //re-arm timer to check ip
            os_timer_setfn(&test_timer, (os_timer_func_t *)user_esp_platform_check_ip, NULL);
            os_timer_arm(&test_timer, 100, 0);
        }
    }
}
 
/******************************************************************************
 * FunctionName : user_set_station_config
 * Description  : set the router info which ESP8266 station will connect to 
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_set_station_config(void)
{
   // Wifi configuration 
   // char ssid[32] = SSID; 
   // char password[64] = PASSWORD; 

    uint8 ssid[] = "FreePackets";
    uint8 password[] = "12345678";

   struct station_config stationConf; 
 
   //need not mac address
   stationConf.bssid_set = 0; 
    
   //Set ap settings 
   // os_memcpy(&stationConf.ssid, ssid, 32); 
   // os_memcpy(&stationConf.password, password, 64); 
    // station_cfg.bssid_set = 1;
    os_strcpy(stationConf.ssid,ssid);
    os_strcpy(stationConf.password,password);

   wifi_station_set_config(&stationConf); 
 
   //set a timer to check whether got ip from router succeed or not.
   os_timer_disarm(&test_timer);
   os_timer_setfn(&test_timer, (os_timer_func_t *)user_esp_platform_check_ip, NULL);
   os_timer_arm(&test_timer, 100, 0);
 
}

/**
 * @brief Test spi interfaces.
 *
 */
void ICACHE_FLASH_ATTR
user_init(void)
{
    
    // PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U,FUNC_GPIO5);

    uint32 io_info[][3] = {{PWM_0_OUT_IO_MUX,PWM_0_OUT_IO_FUNC,PWM_0_OUT_IO_NUM},
                            {PWM_1_OUT_IO_MUX,PWM_1_OUT_IO_FUNC,PWM_1_OUT_IO_NUM}};
    

    // uart_init(115200,115200);

    pwm_init(PWM_Period,MT_PWM,2,io_info);
    pwm_start();

    os_timer_disarm(&sw_timer);
    os_timer_setfn(&sw_timer,sw_timer_cb,NULL);
    os_timer_arm(&sw_timer,500,1); //1:Every 3 secs fun will be called, 0: only once.

    // hw_timer_init(FRC1_SOURCE,1);
    // hw_timer_set_func(hw_test_timer_cb);
    // hw_timer_arm(3000000);

    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U,FUNC_GPIO5);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(5),0);

    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U,FUNC_GPIO1);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U,FUNC_GPIO3);

    // os_printf("\t Akshay Pampatwar \n\r");//spi_interface_test();
    // os_printf("\t Is a rock star \n\r");
    // os_printf("\t SDK version:%s    \n\r", system_get_sdk_version());

    // os_printf("SDK version:%s\n", system_get_sdk_version());
    
   //Set  station mode 
   wifi_set_opmode(STATION_MODE);//STATIONAP_MODE); 
 
   // ESP8266 connect to router.
   user_set_station_config();

}
