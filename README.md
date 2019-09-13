# Smart Roomba with WiFi

## Description
Add smart capabilites to older roomba models (500-600) and communicate via wifi to an MQTT broker.

## Getting started
Edit the wifi, mqtt credentials in the file secrets.h to connect to your network and mqtt broker.
Also change the OTA password in secrets.h and platformio.ini (--auth option). Make sure to not push any passwords to your repo by using the git command below for ech file containing a password.
```
 git update-index --skip-worktree FILENAME
```


By default the platformio.ini is configured to upload wirelessly, for the first flash remove the upload_protocol and upload_port and upload_flags options. Put them back for the OTA update later. 

