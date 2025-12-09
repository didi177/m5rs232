# m5rs232  

## hardware  
M5 Atom lite                            https://shop.m5stack.com/products/atom-lite-esp32-development-kit  
with ATOMIC RS232 Base W/O Atom lite    https://shop.m5stack.com/products/atomic-rs232-base-w-o-atom-lite  

## description  
atom receives data from rs232 and publish it to mqtt-server via wifi  
data have ntp-timestamp and are digital signed with ecdsa to be protected against manipulation and replies

1. receive serial data from a device, add ntp-timestamp, sign-data and send it to a mqtt-server with a m5/esp32 uc
2. receive and verify data from a mqtt-server with a pyhton script 
```
Python-Host  <---  Public MQTT-Server  <---  M5-RS232  <---  v.24/RS232-Device

example data:
topic: m5/50:02:91:A0:7C:40/rs232, message: {"d":"[20251209T072420]hello world !","s":"8rx6nX46hrpT5R04+0Q2DqLawYi2FvKl+wHW/T/KEjDdSV/f87g+4zKWYr+wc7CA0fJl2NdY3QQlPTK/FbvlBw=="}
```
