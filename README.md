# m5rs232  

## hardware  
M5 Atom lite                            https://shop.m5stack.com/products/atom-lite-esp32-development-kit  
with ATOMIC RS232 Base W/O Atom lite    https://shop.m5stack.com/products/atomic-rs232-base-w-o-atom-lite  

## description  
atom receives data from rs232 and publish it at mqtt-server via wifi  
data have ntp-timestamp and are digital signed with ecdsa  

- receive serial data from a device, add ntp-timestamp, sign-data and send it to a mqtt-server with a m5/esp32 uc
- receive and verify data from a mqtt-server with a pyhton script 
```
Python-Host  <---  Public MQTT-Server  <---  M5-RS232  <---  v.24/RS232-Device
```
