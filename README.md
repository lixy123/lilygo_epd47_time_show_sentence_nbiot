# lilygo_epd47_time_show_sentence_nbiot
<b>一.功能：</b> <br/>
1.lilygo 4.7寸墨水屏每日2次从互联网获取一句有趣的话显示<br/> 
2.待机状态低能耗约0.24ma.  每天唤醒2次，电流约70ma, 一般80秒后进入休眠待机状态， 18650电池供电理论预期应在3-5个月<br/>
3.使用NBIOT原因：<br/>
   A.esp32偶尔会连接不上wifi<br/>
   B.有些场景没有AP,或单片机连接受限。例如车上，办公室等<br/>
   
<b>二.硬件</b>  <br/>
1.lilygo-epd47 4.7寸墨水屏 + 锂电池 <br/>
2.2.0mm转2.54mm杜邦线10cm 4线<br/>
3.sim7020c 开发板 <br/>
ESP32  sim7020c (接线)<br/>
3.3V   VBAT<br/>
GND    GND<br/>
12     TXD<br/>
13     RXD<br/>
<img src= 'https://github.com//lilygo_epd47_time_show_sentence_nbiot/blob/main/7.jpg?raw=true' /> <br/>
<img src= 'https://github.com//lilygo_epd47_time_show_sentence_nbiot/blob/main/8.jpg?raw=true' /> <br/>
<b>三.代码:</b><br/>
烧录到ESP32开发板<br/>
A.软件: arduino 1.8.19<br/>
B.用到库文件:<br/>
https://github.com/espressif/arduino-esp32 版本:1.0.6<br/>
https://github.com/bblanchon/ArduinoJson 版本: 6<br/>
https://github.com/adafruit/RTClib RTClib <br/>
https://github.com/Xinyuan-LilyGO/LilyGo-EPD47 墨水屏驱动<br/>
C.开发板选择：TTGO-T-WATCH 参数选默认 (字库文件较大，仅用到其分区定义)<br/>
注： 较新的arduino版本才有这个开发板定义: TTGO T-Watch<br/>
参考：https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library<br/>
D.选择端口，点击烧录<br/>
