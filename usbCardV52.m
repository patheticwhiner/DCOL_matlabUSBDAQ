
clear;
clc;
loadlibrary('Usb_Daq_V52_Dll','Usb_Daq_V52_Dll.h');    % loadlibrary('LibName','HeadFile')加载动态链接库，其中第一个为动态链接库的名字，第二个为头文件
 
x = calllib('Usb_Daq_V52_Dll','openUSB');    % calllib('LibName','Func',arg1,arg2,...,argN)调用动态链接库内部的函数，openUSB(void)无参数输入
 
a = single( 1.0:8.0);    % 初始化一个16元素的一维数组
pv = libpointer('singlePtr', a);    % 用libpointer函数来构造指针 
x = calllib('Usb_Daq_V52_Dll','ad_single',0,1,pv);    % AD_single(int ad_os,int ad_range,float* adResult);
ADsigle = get(pv, 'Value');    % 指针值复制到另一个数组用于显示 
 
  a1=ADsigle(1)
   a2=ADsigle(2)
    a3=ADsigle(3)
     a4=ADsigle(4)
      a5=ADsigle(5)
       a6=ADsigle(6)
        a7=ADsigle(7)
         a8=ADsigle(8)

 
 
% 以10K的采样速率连续采集1024个点，增益为0，过采样0
NumSamp = 1000*8;    % 数据个数必须是8的整数倍，一共16通道，每通道采样1000个数
FrqSamp = 20000;    % 采样频率设为10KHz
b = single(zeros(8,1000));    % 初始化一个8X1000的数组用于保存数据
pv = libpointer('singlePtr', b);    % 用libpointer函数来构造指针 
x = calllib('Usb_Daq_V52_Dll','ad_continu',0,0,FrqSamp,0,0,0,0,NumSamp,pv);    % ad_continu(int ad_os,int ad_range,int freq,int trig_sl,int trig_pol,int clk_sl,int ext_clk_pol,int num,float* databuf);
NumBuf = get(pv, 'Value');    % 将指针数据复制到一个数组内
buf = NumBuf(1,:);    % 显示第一通道数据
plot(buf,'r');
hold on ;
buf = NumBuf(2,:);    % 显示第二通道数据
plot(buf,'b');
% 以上只显示了两个通道数组，添加程序可以把16通道数据全部显示
figure(3);
buf3 = NumBuf(3,:);    % 显示第3通道数据
plot(buf3,'r');
% hold on ;
figure(4);
buf4 = NumBuf(4,:);    % 显示第4通道数据
plot(buf4,'b');
figure(5);
buf5 = NumBuf(5,:);    % 显示第5通道数据
plot(buf5,'r');
% hold on ;
figure(6);
buf6 = NumBuf(6,:);    % 显示第6通道数据
plot(buf6,'b');
figure(7);
buf7 = NumBuf(7,:);    % 显示第7通道数据
plot(buf7,'r');
% hold on ;
figure(8);
buf8 = NumBuf(8,:);    % 显示第8通道数据
plot(buf8,'b');

% IO输入输出
inport = uint16(0);
[x,inport] = calllib('Usb_Daq_V52_Dll','Read_Port_In',inport);    % 读入输入IO口
x = calllib('Usb_Daq_V52_Dll','Write_Port_Out',1);    % 输出口0 变为高电平,其他口为0

% DA输出 
x = calllib('Usb_Daq_V52_Dll','Set_DA_Single',0,5.6);    % 1通道输出5.6V
x = calllib('Usb_Daq_V52_Dll','Set_DA_Single',1,3.8);    % 2通道输出3.8V
calllib('Usb_Daq_V52_Dll','closeUSB'); 
  unloadlibrary( 'Usb_Daq_V52_Dll');    % 从内存中卸载dll库