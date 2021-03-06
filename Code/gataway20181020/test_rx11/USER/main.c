#include "led.h"
#include "delay.h"
#include "sys.h"
#include "usart.h"
#include "spi.h"
#include "port.h"
#include "instance.h"
#include "deca_types.h"
#include "deca_spi.h"
#include "device_info.h" 
#include "stmflash.h"
#include "dma.h"
#include "deca_regs.h"
#include "hw_config.h"
#include "stm32f10x.h"	
#include "stm32f10x_tim.h"
#include "W5500.h"			
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"

#define TX_ANT_DLY 16436
#define RX_ANT_DLY 16436
#define DW1000_SEND_DELAY 200     //DW1000发送间隔
#define DW1000_RECV_TO    (100 * 1000)    //DW1000接收延迟  unit:1us
u8 SendBuff[130];
u8 USB_RxBuff[30];

static uint8 tx_msg[] = {0x41, 0x88, 0, 0xca, 0xde, 0x01, 0xb1, 0x01, 0xa1, 0, 0, 0,0,0};
#define UUS_TO_DWT_TIME 65536	
#define Gateway_DDRL 0x01       /* 网关短地址低位 */
#define Gateway_DDRH 0xB1       /* 网关短地址高位 */
#define PANID 0xde01  
#define PANID_DDRL 0x01      
#define PANID_DDRH 0xde 
static dwt_config_t config = {
    2,               /* Channel number. */
    DWT_PRF_64M,     /* Pulse repetition frequency. */
    DWT_PLEN_1024,   /* Preamble length. Used in TX only. */
    DWT_PAC32,       /* Preamble acquisition chunk size. Used in RX only. */
    9,               /* TX preamble code. Used in TX only. */
    9,               /* RX preamble code. Used in RX only. */
    1,               /* 0 to use standard SFD, 1 to use non-standard SFD. */
    DWT_BR_110K,     /* Data rate. */
    DWT_PHRMODE_STD, /* PHY header mode. */
    1,
	  1057 
};
 struct user_task_t
{
  TaskHandle_t          thread;
  QueueHandle_t        evtq;
  
  bool             isactive;
};
 struct user_task_t dw1000_task;
 struct user_task_t w5500_task;

uint64_t timer3_tick_ms=0; //Timer3定时器计数变量(ms)
unsigned int W5500_Send_Delay_Counter=0; //W5500发送延时计数变量(ms)
uint64_t task_tick_old = 0;
uint64_t task_tick_new = 0;

void Delay(unsigned int d);			//延时函数(ms)


/*******************************************************************************
* 函数名  : W5500_Initialization
* 描述    : W5500初始货配置
* 输入    : 无
* 输出    : 无
* 返回值  : 无
* 说明    : 无
*******************************************************************************/
void W5500_Initialization(void)
{
	W5500_Init();		//初始化W5500寄存器函数
	Detect_Gateway();	//检查网关服务器 
	Socket_Init(0);		//指定Socket(0~7)初始化,初始化端口0
}

/*******************************************************************************
* 函数名  : Load_Net_Parameters
* 描述    : 装载网络参数
* 输入    : 无
* 输出    : 无
* 返回值  : 无
* 说明    : 网关、掩码、物理地址、本机IP地址、端口号、目的IP地址、目的端口号、端口工作模式
*******************************************************************************/
void Load_Net_Parameters(void)
{
	Gateway_IP[0] = 192;//加载网关参数
	Gateway_IP[1] = 168;
	Gateway_IP[2] = 2;
	Gateway_IP[3] = 1;

	Sub_Mask[0]=255;//加载子网掩码
	Sub_Mask[1]=255;
	Sub_Mask[2]=255;
	Sub_Mask[3]=0;

	Phy_Addr[0]=0x0c;//加载物理地址
	Phy_Addr[1]=0x29;
	Phy_Addr[2]=0xab;
	Phy_Addr[3]=0x7c;
	Phy_Addr[4]=0x00;
	Phy_Addr[5]=0x01;

	IP_Addr[0]=192;//加载本机IP地址
	IP_Addr[1]=168;
	IP_Addr[2]=2;
	IP_Addr[3]=5;

	S0_Port[0] = 0x13;//加载端口0的端口号5000 
	S0_Port[1] = 0x88;

//	S0_DIP[0]=192;//加载端口0的目的IP地址
//	S0_DIP[1]=168;
//	S0_DIP[2]=1;
//	S0_DIP[3]=190;
//	
//	S0_DPort[0] = 0x17;//加载端口0的目的端口号6000
//	S0_DPort[1] = 0x70;

	UDP_DIPR[0] = 192;	//UDP(广播)模式,目的主机IP地址
	UDP_DIPR[1] = 168;
	UDP_DIPR[2] = 2;
	UDP_DIPR[3] = 52;
//
	UDP_DPORT[0] = 0x1F;	//UDP(广播)模式,目的主机端口号
	UDP_DPORT[1] = 0x90;

	S0_Mode=UDP_MODE;//加载端口0的工作模式,UDP模式
}

/*******************************************************************************
* 函数名  : W5500_Socket_Set
* 描述    : W5500端口初始化配置
* 输入    : 无
* 输出    : 无
* 返回值  : 无
* 说明    : 分别设置4个端口,根据端口工作模式,将端口置于TCP服务器、TCP客户端或UDP模式.
*			从端口状态字节Socket_State可以判断端口的工作情况
*******************************************************************************/
void W5500_Socket_Set(void)
{
	if(S0_State==0)//端口0初始化配置
	{
		if(S0_Mode==TCP_SERVER)//TCP服务器模式 
		{
			if(Socket_Listen(0)==TRUE)
				S0_State=S_INIT;
			else
				S0_State=0;
		}
		else if(S0_Mode==TCP_CLIENT)//TCP客户端模式 
		{
			if(Socket_Connect(0)==TRUE)
				S0_State=S_INIT;
			else
				S0_State=0;
		}
		else//UDP模式 
		{
			if(Socket_UDP(0)==TRUE)
				S0_State=S_INIT|S_CONN;
			else
				S0_State=0;
		}
	}
}

/*******************************************************************************
* 函数名  : Process_Socket_Data
* 描述    : W5500接收并发送接收到的数据
* 输入    : s:端口号
* 输出    : 无
* 返回值  : 无
* 说明    : 本过程先调用S_rx_process()从W5500的端口接收数据缓冲区读取数据,
*			然后将读取的数据从Rx_Buffer拷贝到Temp_Buffer缓冲区进行处理。
*			处理完毕，将数据从Temp_Buffer拷贝到Tx_Buffer缓冲区。调用S_tx_process()
*			发送数据。
*******************************************************************************/
void Process_Socket_Data(SOCKET s)
{
	unsigned short size;
	size=Read_SOCK_Data_Buffer(s, Rx_Buffer);
	UDP_DIPR[0] = Rx_Buffer[0];
	UDP_DIPR[1] = Rx_Buffer[1];
	UDP_DIPR[2] = Rx_Buffer[2];
	UDP_DIPR[3] = Rx_Buffer[3];

	UDP_DPORT[0] = Rx_Buffer[4];
	UDP_DPORT[1] = Rx_Buffer[5];
	memcpy(Tx_Buffer, Rx_Buffer+8, size-8);			
	Write_SOCK_Data_Buffer(s, Tx_Buffer, size);
}
void Timer3_Init(u16 arr,u16 psc)
{
     TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
     NVIC_InitTypeDef NVIC_InitStructure;
 
     RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
 
     TIM_TimeBaseStructure.TIM_Period = (arr - 1); 
     TIM_TimeBaseStructure.TIM_Prescaler =(psc-1); 
     TIM_TimeBaseStructure.TIM_ClockDivision = 0; 
     TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  
     TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure); 
  
      
     TIM_ITConfig(  
         TIM3, 
         TIM_IT_Update  |  
         TIM_IT_Trigger,  
         ENABLE  
         );
      
     NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;  //TIM3??
     NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;  
     NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;  
     NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 
     NVIC_Init(&NVIC_InitStructure);  
 
     TIM_Cmd(TIM3, ENABLE);  
                              
 }
 
 void TIM3_IRQHandler(void)   
 {
     if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) 
         {
            TIM_ClearITPendingBit(TIM3, TIM_IT_Update);  
            timer3_tick_ms += 10;
         }
}
void vApplicationIdleHook( void )
{
}

void vApplicationTickHook(void)
{
}
void vApplicationStackOverflowHook(TaskHandle_t xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
  __nop;
}
static uint64_t TRK_COUNTER ;
static uint64_t SYNC_COUNTER;
static uint64_t test3;
void  w5500_recv_task_thead(void * arg)
{
    W5500_Socket_Set();//W5500端口初始化配置
    W5500_Interrupt_Process();//W5500中断处理程序框架
	w5500_task.isactive = 1;
	while (w5500_task.isactive)
    {
        vTaskDelay(10);
        if((S0_Data & S_RECEIVE) == S_RECEIVE)//如果Socket0接收到数据
        {
            S0_Data&=~S_RECEIVE;
            Process_Socket_Data(0);//W5500接收并发送接收到的数据
        }
    }
    
}
void  dw1000_task_thead(void * arg)
{
    u8 eui64[2];
	int i;
	static uint8 rx_buffer[127];
	static uint32 status_reg = 0;
	static uint16 frame_len = 0;

	u8 message[15];
	uint8 t1[5],t2[5];
	u8 tag_f;
	int num=0;             //接收信息戳数量 包含延时
    int time_slot = 0;
    
    
    
        
		SPI_ConfigFastRate(SPI_BaudRatePrescaler_32);
		reset_DW1000(); 	
		port_SPIx_clear_chip_select();
		delay_ms(110);
		port_SPIx_set_chip_select();
		if (dwt_initialise(DWT_LOADUCODE | DWT_LOADLDOTUNE | DWT_LOADTXCONFIG | DWT_LOADANTDLY| DWT_LOADXTALTRIM)  == DWT_ERROR)
		{	
			led_on(LED_ALL);
			i=10;
			while (i--)
			{
				led_on(LED_ALL);
				delay_ms(200);
				led_off(LED_ALL);
				delay_ms(200);
			};
		}
 	    dwt_setleds(3) ; 
        dwt_configure(&config,DWT_LOADXTALTRIM);	
        eui64[0]=Gateway_DDRL;         //定义网关地址
	    eui64[1]=Gateway_DDRH;
	    dwt_seteui(eui64);
        dwt_setpanid(PANID);
        tx_msg[4]=PANID_DDRH;
        tx_msg[3]=PANID_DDRL;
        tx_msg[5]=0xff;
        tx_msg[6]=0xff;
        tx_msg[7]=Gateway_DDRL;
        tx_msg[8]=Gateway_DDRH;

		led_on(LED_ALL);
        port_EnableEXT_IRQ();
        dwt_setrxantennadelay(0);
        dwt_settxantennadelay(0);	
        //    dwt_setrxtimeout(60000);	
    
    task_tick_new = timer3_tick_ms;
	dw1000_task.isactive = 1;
    
	while (dw1000_task.isactive)
    {
//        vTaskDelay(10);
        memset(rx_buffer,0,sizeof(rx_buffer));
        task_tick_new = timer3_tick_ms; 
        time_slot += (task_tick_new - task_tick_old);
        if(time_slot > DW1000_SEND_DELAY)
        {
            time_slot = 0;
//				    delay_ms(50);
//					dwt_readsystime(t2);	
//					for(i=9;i<14;i++)
//					{
//					    tx_msg[i]=t2[i-9];
//				 	}	 
            dwt_writetxdata(sizeof(tx_msg), tx_msg, 0); /* Zero offset in TX buffer. */
            dwt_writetxfctrl(sizeof(tx_msg), 0); /* Zero offset in TX buffer, no ranging. */            
            dwt_starttx(DWT_START_TX_IMMEDIATE); 
            while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS))
            { 
                
            };
            SYNC_COUNTER ++;
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
            dwt_readtxtimestamp(t2);
            message[1]=Gateway_DDRL;
            message[2]=Gateway_DDRH;
            message[2]=0xaa;
            message[3]=tx_msg[2];
            for(i=4;i<9;i++)
            {
                 message[i]=t2[i-4];
            }
            message[9]=0xff;
            tx_msg[2]++;
              Write_SOCK_Data_Buffer(0,message,10);
//			    USB_TxWrite(tx_msg,15);				
        }
        else
        {
            dwt_setrxtimeout(50*1000);	
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
            led_on(LED_ALL);
            while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & (SYS_STATUS_RXFCG | (SYS_STATUS_RXPHE | SYS_STATUS_RXFCE | SYS_STATUS_RXRFSL | SYS_STATUS_RXSFDTO | SYS_STATUS_RXRFTO \
                                                            | SYS_STATUS_AFFREJ | SYS_STATUS_LDEERR))))
            {
                __NOP;
            };
            if (status_reg & SYS_STATUS_RXFCG)
            {
                frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
                if (frame_len <= 127)
                {
                    dwt_readrxdata(rx_buffer, frame_len, 0);
                }
                dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
                dwt_readrxtimestamp(t1);
                if(rx_buffer[4]==0xff&&rx_buffer[5]==0xff)  //接收信息为补偿帧
                {
                    message[1]=Gateway_DDRL;
                    message[0]=Gateway_DDRH;
                    message[2]=0xad;
                    message[3]=rx_buffer[1];
                    message[4]=rx_buffer[6];
                    message[5]=rx_buffer[7];
                    for(i=6;i<11;i++)
                    {
                        message[i]=t1[i-6];
                    }
                    message[11]=0xff;
                    num++;
                    Write_SOCK_Data_Buffer(0, message, 12);
    //						USB_TxWrite(message,12);
                }
                else if(rx_buffer[4]==0xee&&rx_buffer[5]==0xee)
                {
                    message[1]=Gateway_DDRL;
                    message[0]=Gateway_DDRH;
                    message[2]=0xab;
                    message[3]=rx_buffer[1];
                    message[4]=rx_buffer[2];
                    message[5]=rx_buffer[3];
                    for(i=6;i<11;i++)
                    {
                        message[i]=t1[i-6];
                    }
                    message[11]=0xff;
                    TRK_COUNTER ++;

                    USB_TxWrite(message,12);
                    Write_SOCK_Data_Buffer(0, message, 12);
                    num++;       //接收信息为定位帧时计数
                    tag_f=rx_buffer[8];   //定位标签发送频率
                }
         
                }					
            else
            {
                /* Clear RX error events in the DW1000 status register. */
                dwt_write32bitreg(SYS_STATUS_ID, (SYS_STATUS_RXPHE | SYS_STATUS_RXFCE | SYS_STATUS_RXRFSL | SYS_STATUS_RXSFDTO | SYS_STATUS_RXRFTO\
                                                                 | SYS_STATUS_AFFREJ | SYS_STATUS_LDEERR));
                num++;
            }
        }
        task_tick_old = task_tick_new;
	}

}
uint32_t dw1000_task_start ()
{
  if (dw1000_task.isactive)
    return 1;

  // Start execution.
  if (pdPASS != xTaskCreate (dw1000_task_thead, "DW1000", 256, &dw1000_task, 4, &dw1000_task.thread))
  {
    return 1;
  }
  return 0;
}
uint32_t w5500_task_start ()
{
  if (w5500_task.isactive)
    return 1;

  // Start execution.
  if (pdPASS != xTaskCreate (w5500_recv_task_thead, "W5500", 256, &w5500_task, 5, &w5500_task.thread))
  {
    return 1;
  }
  return 0;
}
	
int main(void)
{ 
     peripherals_init();
	delay_init();
	uart_init(115200);	 //串口初始化为115200 	
    GPIO_Configuration();//初始化与LED连接的硬件接口
    SPI_Configuration();
    Timer3_Init(100,7200);  //10ms
    delay_ms(1000);
    Flash_Configuration();
    delay_ms(200);
    USB_Config();
    MYDMA_Config(DMA1_Channel4,(u32)&USART1->DR,(u32)SendBuff,130);//DMA1通道4,外设为串口1,存储器为SendBuff,长度130.  
    USART_DMACmd(USART1,USART_DMAReq_Tx,ENABLE); //使能串口1的DMA发送  
    MYDMA_Enable(DMA1_Channel4);//开始一次DMA传输！		

    SPI2_Configuration();		//W5500 SPI初始化配置(STM32 SPI1)
    W5500_GPIO_Configuration();	//W5500 GPIO初始化配置	
    Load_Net_Parameters();		//装载网络参数	
    W5500_Hardware_Reset();		//硬件复位W5500
    W5500_Initialization();		//W5500初始货配置	
    dw1000_task_start();
	w5500_task_start();
    vTaskStartScheduler();
    while (1)
    {   
    }
		
}	
	
/*******************************************************************************
* 函数名  : Delay
* 描述    : 延时函数(ms)
* 输入    : d:延时系数，单位为毫秒
* 输出    : 无
* 返回    : 无 
* 说明    : 延时是利用Timer2定时器产生的1毫秒的计数来实现的
*******************************************************************************/
//void Delay(unsigned int d)
//{
//	unsigned char tt=0;
//	Timer2_Counter=0; 
//  while(1)
//	{
//		if (Timer2_Counter>=d ) 
//{
//break;
//}
//	}
//	tt=0;
//}

void Delay(unsigned int d)
{
	delay_ms(d);
	
}


