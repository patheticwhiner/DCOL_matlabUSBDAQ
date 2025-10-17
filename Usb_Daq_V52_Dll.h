// 下列 ifdef 块是创建使从 DLL 导出更简单的
// 宏的标准方法。此 DLL 中的所有文件都是用命令行上定义的 USB_DAQ_V52_DLL_EXPORTS
// 符号编译的。在使用此 DLL 的
// 任何其他项目上不应定义此符号。这样，源文件中包含此文件的任何其他项目都会将
// USB_DAQ_V52_DLL_API 函数视为是从 DLL 导入的，而此 DLL 则将用此宏定义的
// 符号视为是被导出的。
#ifdef USB_DAQ_V52_DLL_EXPORTS
#define USB_DAQ_V52_DLL_API extern "C" __declspec(dllexport)
#else
#define USB_DAQ_V52_DLL_API extern "C" __declspec(dllimport)
#endif
int openUSB(void);
void closeUSB(void);
void Reset(void);
int get_device_num(void);
int set_current_device(int num);
int ad_single(int ad_os,int ad_range,float*  databuf);
int  ad_continu_conf(int ad_os,int ad_range,int freq,int trig_sl,int trig_pol,int clk_sl,int ext_clk_pol);
int Get_AdBuf_Size(void); 
int Read_AdBuf(float* databuf,int num);
int AD_continu_stop(void);
int ad_continu(int ad_os,int ad_range,int freq,int trig_sl,int trig_pol,int clk_sl,int ext_clk_pol,int num,float* databuf);
int Pwm_Out(int ch,int en,int freq,float duty);//ch0--3
int Pulse_Out(int ch,int pulse);
int Set_Pwm_In(int ch,int en);
int Read_Pwm_In(int ch,float* freq,float* duty);
 
int Read_Port_In(unsigned short* in_port);
int Read_Port_Out(unsigned short* out_port);


int Write_Port_Out(unsigned short out_port);
int Set_Port_Out(unsigned short out_port); 
int Reset_Port_Out(unsigned short out_port);
int Write_Port_OutL(unsigned char out_port);
int Write_Port_OutH(unsigned char out_port);

int Set_DA_Single(int ch,float da_value);
 
