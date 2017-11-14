BLE-Mesh - a BLE-MESH light example
===================================

A simple open source lightbulb project built on the Open Source Foundries Zephyr microPlatform.  This sample is built against the latest public releases available at [Open Source Foundries on Github](http://github.com/opensourcefoundries.com).

----------
To rebuild the OSLight applications, setup your environment according to Open Source Foundries Zephyr microPlatform guidelines and then you can use repo and the zmp command-line application to rebuild and flash.  The following samples work on MacOS and Linux.

Setup your workspace and get the code
```
#Create your workspace
mkdir oslight-workingdirectory
cd oslight-workingdirectory

#get the Zephyr microPlatform
repo init -u https://github.com/OpenSourceFoundries/zmp-manifest
repo sync

#get the lwm2m zephyr app
git clone https://github.com/oslight/lwm2m-light

#get the ble-mesh sample app
git clone https://github.com/oslight/ble-mesh

```
Build the application(s):
```
./zmp build -b nrf52_blenano2 lwm2m-light/
or
./zmp build -b nrf52_blenano2 ble-mesh/
```
Flash the code to the BLE Nano2:
```
./zmp flash -b nrf52_blenano2 lwm2m-light/
or
./zmp flash -b nrf52_blenano2 ble-mesh/
```
