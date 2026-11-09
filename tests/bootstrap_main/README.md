# Application bootstrap tests

Run `sh tests/bootstrap_main/run.sh` to compile the production `main/app_main.c`
with the real MyBot/platform/RTSA public headers and mocked ESP-IDF boundaries.

Seven UBSan scenarios cover failed startup and normal shutdown, immediate or
retried cleanup, permanent cleanup failure, and Wi-Fi provisioning. Network
shutdown, SNTP shutdown and provisioning assert that SDK resources were released
first. Permanent failure must leave the network available and stop retrying after
three calls. These host tests do not measure hardware shutdown timing.
