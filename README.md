# pi_backup
Command line backup for Raspbian (copied from https://github.com/raspberrypi-ui/piclone/blob/master/src/backup in case it is removed.)  This creates a file-by-file backup of your Raspbian SD card so will work with a larger or smaller target SD card but takes longer than creating an image with a tool like win32diskimager.

To use:
1. Clone this repo to your Raspberry Pi with:
```sh
git clone https://github.com/nwright-mcc/pi_backup.git
```
2. Insert a backup SD card in a USB card reader in any USB connector of the Pi. The SD card must be large enough to hold all of your files on your existing Pi SD card.
3. Determine what device name the SD card uses:
```sh
ls /dev/sd*
```
You should see /dev/sda and /dev/sda1 (and possibly more, depending on how many partitions the SD card has.)  If there are more, such as /dev/sdb, then you must remove the other USB media or determine which device matches your USB reader.
4. Go to the pi_backup directory then run backup, specifying the correct device name from #3 above:
```sh
cd ~/pi_backup
sudo ./backup /dev/sda
```
