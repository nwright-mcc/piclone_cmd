# piclone_cmd
Command line version of piclone from Raspbian (copied from https://github.com/raspberrypi-ui/piclone/ and modified.)  This creates a file-by-file backup of your Raspbian SD card or any other device so will work with a larger or smaller target SD card.

To use:
1. Clone this repo to your Raspberry Pi with:
   ```sh
   git clone https://github.com/nwright-mcc/piclone_cmd.git
   ```
   If you do not have git installed, first run:
   ```sh
   sudo apt-get install git
   ```
2. Install uuid
   ```sh
   sudo apt-get install uuid
   ```
3. Insert a backup SD card in a USB card reader in any USB connector of the Pi. The SD card must be large enough to hold all of your files    on your existing Pi SD card.
3. Determine what device name the SD card uses:
   ```sh
   ls /dev/sd*
   ```
   You should see /dev/sda and /dev/sda1 (and possibly more, depending on how many partitions the SD card has.)  If there are more, such    as /dev/sdb, then you must remove the other USB media or determine which device matches your USB reader.
4. Go to the pi_backup directory then run piclone_cmd, specifying the correct device name from #3 above:
   ```sh
   cd ~/piclone_cmd
   sudo ./piclone_cmd /dev/sda
   ```
5. The program will display status and progress.
