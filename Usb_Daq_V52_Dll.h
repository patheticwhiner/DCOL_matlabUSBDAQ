// 下列 ifdef 块是创建使从 DLL 导出更简单的
// 宏的标准方法。此 DLL 中的所有文件都是用命令行上定义的 USB_DAQ_V52_DLL_EXPORTS
// 符号编译的。在使用此 DLL 的
// 任何其他项目上不应定义此符号。这样，源文件中包含此文件的任何其他项目都会将
// USB_DAQ_V52_DLL_API 函数视为是从 DLL 导入的，而此 DLL 则将用此宏定义的
// 符号视为是被导出的。

#ifdef __cplusplus
extern "C" {
#endif

#ifdef USB_DAQ_V52_DLL_EXPORTS
#define USB_DAQ_V52_DLL_API __declspec(dllexport)
#else
#define USB_DAQ_V52_DLL_API __declspec(dllimport)
#endif

USB_DAQ_V52_DLL_API  int openUSB(void);
USB_DAQ_V52_DLL_API  int closeUSB(void);

USB_DAQ_V52_DLL_API  int get_device_num(void);
USB_DAQ_V52_DLL_API int Reset_Usb_Device(int dev); 
USB_DAQ_V52_DLL_API  int ad_single(int dev,int ad_os,int ad_range,float*  databuf);
USB_DAQ_V52_DLL_API  int  ad_continu_conf(int dev,int ad_os,int ad_range,int ch_first,int ch_last,int freq,int trig_sl,int trig_pol,int clk_sl,int ext_clk_pol);
USB_DAQ_V52_DLL_API int Get_AdBuf_Size(int dev); 
USB_DAQ_V52_DLL_API int Read_AdBuf(int dev,float* databuf,int num);
USB_DAQ_V52_DLL_API int AD_continu_stop(int dev);
USB_DAQ_V52_DLL_API  int ad_continu(int dev,int ad_os,int ad_range,int ch_first,int ch_last,int freq,int trig_sl,int trig_pol,int clk_sl,int ext_clk_pol,int num,float* databuf);
USB_DAQ_V52_DLL_API  int Pwm_Out(int dev,int ch,int en,int freq,float duty);//ch0--3
USB_DAQ_V52_DLL_API  int Pulse_Out(int dev,int ch,int pulse);
USB_DAQ_V52_DLL_API  int Set_Pwm_In(int dev,int ch,int en);
USB_DAQ_V52_DLL_API  int Read_Pwm_In(int dev,int ch,float* freq,float* duty);
 
USB_DAQ_V52_DLL_API  int Read_Port_In(int dev,unsigned short* in_port);
USB_DAQ_V52_DLL_API  int Read_Port_Out(int dev,unsigned short* out_port);


USB_DAQ_V52_DLL_API  int Write_Port_Out(int dev,unsigned short out_port);
USB_DAQ_V52_DLL_API  int Set_Port_Out(int dev,unsigned short out_port); 
USB_DAQ_V52_DLL_API  int Reset_Port_Out(int dev,unsigned short out_port);
USB_DAQ_V52_DLL_API  int Write_Port_OutL(int dev,unsigned char out_port);
USB_DAQ_V52_DLL_API  int Write_Port_OutH(int dev,unsigned char out_port);

USB_DAQ_V52_DLL_API int Set_DA_Single(int dev,int ch,float da_value);

#ifdef __cplusplus
}
#endif
 