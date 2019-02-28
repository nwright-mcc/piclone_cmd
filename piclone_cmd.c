/*
piclone_cmd.c - Modified version of Raspberry Pi piclone application for command
                line use.
Modified by Nick Wright, 02/28/2019

Original copyright for piclone.c:

Copyright (c) 2018 Raspberry Pi (Trading) Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>

/*---------------------------------------------------------------------------*/
/* Variable and macro definitions */
/*---------------------------------------------------------------------------*/

/* struct to store partition data */

#define MAXPART 9

typedef struct
{
    int pnum;
    long start;
    long end;
    char ptype[10];
    char ftype[20];
    char flags[10];
} partition_t;

partition_t parts[MAXPART];

/* combo box counters */
int src_count, dst_count;

/* device names */
char src_dev[32], dst_dev[32];

/* mount points */
char src_mnt[32], dst_mnt[32];

/* flag to show that new partition UUIDs should be created */
char new_uuid;

/* flag to show that copy thread is running */
char copying;

/* flag to show that copy has been interrupted */
char ended;

/* flag to show that backup has been cancelled by the user */
volatile char cancelled;
#define CANCEL_CHECK if (cancelled) { return 1; }


/*---------------------------------------------------------------------------*/
/* Function definitions */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/* System helpers */

void show_progress(double fraction)
{
    int percent;
    int num_chars;
    int i;
    
    num_chars = (int)(fraction * 50);
    percent = (int)(fraction * 100.0);
    
    printf("\r[");
    for (i = 0; i < num_chars; i++)
    {
        printf("#");
    }
    for (; i < 50; i++)
    {
        printf(" ");
    }
    printf("] (%d%%)", percent);
    fflush(stdout);
}

/* Call a system command and read the first string returned */

static int get_string (char *cmd, char *name)
{
    FILE *fp;
    char buf[64];
    int res;

    name[0] = 0;
    fp = popen (cmd, "r");
    if (fp == NULL) return 0;
    if (fgets (buf, sizeof (buf) - 1, fp) == NULL)
    {
        pclose (fp);
        return 0;
    }
    else
    {
        pclose (fp);
        res = sscanf (buf, "%s", name);
        if (res != 1) return 0;
        return 1;
    }
}

/* System function with printf formatting */

static int sys_printf (const char * format, ...)
{
    char buffer[256];
    va_list args;
    FILE *fp;

    va_start (args, format);
    vsprintf (buffer, format, args);
    fp = popen (buffer, "r");
    va_end (args);
    while (fgets(buffer, sizeof(buffer) - 1, fp) != NULL)
    {
        //printf(buffer);
    }
    return pclose (fp);
}

/* Get a partition name - format is different on mmcblk from sd */

static char *partition_name (char *device, char *buffer)
{
    if (!strncmp (device, "/dev/mmcblk", 11))
        sprintf (buffer, "%sp", device);
    else
        sprintf (buffer, "%s", device);
    return buffer;
}

void int_handler(int dummy)
{
    cancelled = 1;
}

/*---------------------------------------------------------------------------*/
/* Threads */

/* Thread which calls the system copy command to do the bulk of the work */

static void* copy_thread (void* data)
{
    copying = 1;
    sys_printf ("cp -ax %s/. %s/.", src_mnt, dst_mnt);
    copying = 0;
    return NULL;
}

void print_usage(void)
{
    printf("Usage: piclone_cmd [-u] [-i source] dest\n");
    printf("Clone a Raspberry Pi SD card or other device to a different device.\n");
    printf("Default source is /dev/mmcblk0.\n");
    printf("The destination device must be specified, typically /dev/sda.\n\n");
    printf("  -u\tReuse the media UUID. Stretch and beyond do not allow mounting devices\n");
    printf("    \twith the same UUID, so this option is not recommended for boot media.\n");
    printf("  -i\tSpecify the source disk if not /dev/mmcblk0.\n");
}

int main(int argc, char* argv[])
{
    char buffer[256], res[256], dev[16], uuid[64], puuid[64], npuuid[64];
    int n, p, lbl, uid, puid;
    long srcsz, dstsz, stime;
    double prog;
    FILE *fp;
    pthread_t thread_id;
    int opt;
    int dest_specified;
    
    new_uuid = 1;
    sprintf(src_dev, "/dev/mmcblk0");
    dest_specified = 0;
    
    while ((opt = getopt(argc, argv, ":ui:")) != -1)
    {
        switch (opt)
        {
        case 'u':
            // reuse UUID
            new_uuid = 0;
            break;
        case 'i':
            // specify source (uses /dev/mmcblk0 if not specified)
            strcpy(src_dev, optarg);
            break;
        default:
            break;
        }
    }
    
    for (; optind < argc; optind++)
    {
        dest_specified = 1;
        strcpy(dst_dev, argv[optind]);
    }

    if (!dest_specified)
    {
        print_usage();
        return 1;
    }

    signal(SIGINT, int_handler);
    
    
    // get a new partition UUID
    get_string ("uuid | cut -f1 -d-", npuuid);

    // check the source has an msdos partition table
    sprintf (buffer, "parted %s unit s print | tail -n +4 | head -n 1", src_dev);
    fp = popen (buffer, "r");
    if (fp == NULL) return 1;
    if (fgets (buffer, sizeof (buffer) - 1, fp) == NULL)
    {
        pclose (fp);
        printf("Unable to read source.\n");
        return 1;
    }
    pclose (fp);
    if (strncmp (buffer, "Partition Table: msdos", 22))
    {
        printf("Non-MSDOS partition table on source.\n");
        return 1;
    }
    else
    CANCEL_CHECK;

    printf("Preparing target...\n");
    
    // unmount any partitions on the target device
    for (n = 9; n >= 1; n--)
    {
        sys_printf ("umount %s%d >/dev/null 2>&1", partition_name (dst_dev, dev), n);
        CANCEL_CHECK;
    }

    // wipe the FAT on the target
    if (sys_printf ("dd if=/dev/zero of=%s bs=512 count=1  >/dev/null 2>&1", dst_dev))
    {
        printf("Could not write to destination.\n");
        return 1;
    }
    CANCEL_CHECK;
    
    // prepare temp mount points
    get_string ("mktemp -d", src_mnt);
    CANCEL_CHECK;
    get_string ("mktemp -d", dst_mnt);
    CANCEL_CHECK;
    
    // prepare the new FAT
    if (sys_printf ("parted -s %s mklabel msdos", dst_dev))
    {
        printf("Could not create FAT.\n");
        return 1;
    }
    CANCEL_CHECK;
    
    printf("Reading partitions...\n");

    // read in the source partition table
    n = 0;
    sprintf (buffer, "parted %s unit s print | sed '/^ /!d'", src_dev);
    fp = popen (buffer, "r");
    if (fp != NULL)
    {
        while (1)
        {
            if (fgets (buffer, sizeof (buffer) - 1, fp) == NULL) break;
            if (n >= MAXPART)
            {
                pclose (fp);
                printf("Too many partitions on source.\n");
                return 1;
            }
            sscanf (buffer, "%d %lds %lds %*ds %s %s %s", &(parts[n].pnum), &(parts[n].start),
                &(parts[n].end), (char *) &(parts[n].ptype), (char *) &(parts[n].ftype), (char *) &(parts[n].flags));
            n++;
        }
        pclose (fp);
    }
    CANCEL_CHECK;
    
    printf("Preparing partitions...\n");
    //show_progress(0.0);
    
    // recreate the partitions on the target
    for (p = 0; p < n; p++)
    {
        // create the partition
        if (!strcmp (parts[p].ptype, "extended"))
        {
            if (sys_printf ("parted -s %s -- mkpart extended %lds -1s", dst_dev, parts[p].start))
            {
                printf("Could not create partition.\n");
                return 1;
            }
        }
        else
        {
            if (p == (n - 1))
            {
                if (sys_printf ("parted -s %s -- mkpart %s %s %lds -1s", dst_dev,
                    parts[p].ptype, parts[p].ftype, parts[p].start))
                {
                    printf("Could not create partition.\n");
                    return 1;
                }
            }
            else
            {
                if (sys_printf ("parted -s %s mkpart %s %s %lds %lds", dst_dev,
                    parts[p].ptype, parts[p].ftype, parts[p].start, parts[p].end))
                {
                    printf("Could not create partition.\n");
                    return 1;
                }
            }
        }
        CANCEL_CHECK;

        // refresh the kernel partion table
        sys_printf("partprobe %s", dst_dev);
        CANCEL_CHECK;

        // get the UUID
        sprintf (buffer, "lsblk -o name,uuid %s | grep %s%d | tr -s \" \" | cut -d \" \" -f 2", src_dev, partition_name (src_dev, dev) + 5, parts[p].pnum);
        uid = get_string (buffer, uuid);
        if (uid)
        {
            // sanity check the ID
            if (strlen (uuid) == 9)
            {
                if (uuid[4] == '-')
                {
                    // remove the hyphen from the middle of a FAT volume ID
                    uuid[4] = uuid[5];
                    uuid[5] = uuid[6];
                    uuid[6] = uuid[7];
                    uuid[7] = uuid[8];
                    uuid[8] = 0;
                }
                else uid = 0;
            }
            else if (strlen (uuid) == 36)
            {
                // check there are hyphens in the right places in a UUID
                if (uuid[8] != '-') uid = 0;
                if (uuid[13] != '-') uid = 0;
                if (uuid[18] != '-') uid = 0;
                if (uuid[23] != '-') uid = 0;
            }
            else uid = 0;
        }

        // get the label
        sprintf (buffer, "lsblk -o name,label %s | grep %s%d | tr -s \" \" | cut -d \" \" -f 2", src_dev, partition_name (src_dev, dev) + 5, parts[p].pnum);
        lbl = get_string (buffer, res);
        if (!strlen (res)) lbl = 0;

        // get the partition UUID
        sprintf (buffer, "blkid %s | rev | cut -f 2 -d ' ' | rev | cut -f 2 -d \\\"", src_dev);
        puid = get_string (buffer, puuid);
        if (!strlen (puuid)) puid = 0;

        // create file systems
        if (!strncmp (parts[p].ftype, "fat", 3))
        {
            if (uid) sprintf (buffer, "mkfs.fat -i %s %s%d", uuid, partition_name (dst_dev, dev), parts[p].pnum);
            else sprintf (buffer, "mkfs.fat %s%d", partition_name (dst_dev, dev), parts[p].pnum);

            if (sys_printf (buffer))
            {
                if (uid)
                {
                    // second try just in case the only problem was a corrupt UUID
                    sprintf (buffer, "mkfs.fat %s%d", partition_name (dst_dev, dev), parts[p].pnum);
                    if (sys_printf (buffer))
                    {
                        printf("Could not create file system.\n");
                        return 1;
                    }
                }
                else
                {
                    printf("Could not create file system.\n");
                    return 1;
                }
            }

            if (lbl) 
            {
                sys_printf ("fatlabel %s%d %s >/dev/null 2>&1", partition_name (dst_dev, dev), parts[p].pnum, res);
            }
        }
        CANCEL_CHECK;

        if (!strcmp (parts[p].ftype, "ext4"))
        {
            if (uid) sprintf (buffer, "mkfs.ext4 -F -U %s %s%d", uuid, partition_name (dst_dev, dev), parts[p].pnum);
            else sprintf (buffer, "mkfs.ext4 -F %s%d", partition_name (dst_dev, dev), parts[p].pnum);

            if (sys_printf (buffer))
            {
                if (uid)
                {
                    // second try just in case the only problem was a corrupt UUID
                    sprintf (buffer, "mkfs.ext4 -F %s%d", partition_name (dst_dev, dev), parts[p].pnum);
                    if (sys_printf (buffer))
                    {
                        printf("Could not create file system.\n");
                        return 1;
                    }
                }
                else
                {
                    printf("Could not create file system.\n");
                    return 1;
                }
            }

            if (lbl) sys_printf ("e2label %s%d %s", partition_name (dst_dev, dev), parts[p].pnum, res);
        }
        CANCEL_CHECK;

        // write the partition UUID
        if (puid) sys_printf ("echo \"x\ni\n0x%s\nr\nw\n\" | fdisk %s", new_uuid ? npuuid : puuid, dst_dev);

        // set the flags        
        if (!strcmp (parts[p].flags, "lba"))
        {
            if (sys_printf ("parted -s %s set %d lba on", dst_dev, parts[p].pnum))
            {
                printf("Could not set flags.\n");
                return 1;
            }
        }
        else
        {
            if (sys_printf ("parted -s %s set %d lba off", dst_dev, parts[p].pnum))
            {
                printf("Could not set flags.\n");
                return 1;
            }
        }
        CANCEL_CHECK;
        
        //prog = p + 1;
       // prog /= n;
        //show_progress(prog);
    }
    //printf("\n");
    
    // do the copy for each partition
    for (p = 0; p < n; p++)
    {
        // don't try to copy extended partitions
        if (!strcmp (parts[p].ptype, "extended")) continue;

        printf("\nCopying partition %d of %d...\n", p + 1, n);
        show_progress(0.0);
        
        // mount partitions
        if (sys_printf ("mount %s%d %s", partition_name (dst_dev, dev), parts[p].pnum, dst_mnt))
        {
            printf("\nCould not mount partition.\n");
            return 1;
        }
        CANCEL_CHECK;
        if (sys_printf ("mount %s%d %s", partition_name (src_dev, dev), parts[p].pnum, src_mnt))
        {
            printf("\nCould not mount partition.\n");
            return 1;
        }
        CANCEL_CHECK;

        // check there is enough space...
        sprintf (buffer, "df %s | tail -n 1 | tr -s \" \" \" \" | cut -d ' ' -f 3", src_mnt);
        get_string (buffer, res);
        sscanf (res, "%ld", &srcsz);

        sprintf (buffer, "df %s | tail -n 1 | tr -s \" \" \" \" | cut -d ' ' -f 4", dst_mnt);
        get_string (buffer, res);
        sscanf (res, "%ld", &dstsz);

        if (srcsz >= dstsz)
        {
            sys_printf ("umount %s", dst_mnt);
            sys_printf ("umount %s", src_mnt);
            printf("\nInsufficient space. Backup aborted.\n");
            return 1;
        }

        // start the copy itself in a new thread
        pthread_create(&thread_id, NULL, copy_thread, NULL);
        
        // get the size to be copied
        sprintf (buffer, "du -s %s", src_mnt);
        get_string (buffer, res);
        sscanf (res, "%ld", &srcsz);
        if (srcsz < 50000) stime = 1;
        else if (srcsz < 500000) stime = 5;
        else stime = 10;

        // wait for the copy to complete
        sprintf (buffer, "du -s %s", dst_mnt);
        while (copying)
        {
            get_string (buffer, res);
            sscanf (res, "%ld", &dstsz);
            prog = dstsz;
            prog /= srcsz;
            show_progress(prog);
            sleep (stime);
            CANCEL_CHECK;
        }

        pthread_join(thread_id, NULL);
        show_progress(1.0);

        // fix up relevant files if changing partition UUID
        if (puid && new_uuid)
        {
            // relevant files are dst_mnt/etc/fstab and dst_mnt/boot/cmdline.txt
            sys_printf ("if [ -e /%s/etc/fstab ] ; then sed -i s/%s/%s/g /%s/etc/fstab ; fi", dst_mnt, puuid, npuuid, dst_mnt);
            sys_printf ("if [ -e /%s/cmdline.txt ] ; then sed -i s/%s/%s/g /%s/cmdline.txt ; fi", dst_mnt, puuid, npuuid, dst_mnt);
        }

        // unmount partitions
        if (sys_printf ("umount %s", dst_mnt))
        {
            printf("\nCould not unmount partition.\n");
            return 1;
        }
        CANCEL_CHECK;
        if (sys_printf ("umount %s", src_mnt))
        {
            printf("Could not unmount partition.\n");
            return 1;
        }
        CANCEL_CHECK;
    }

    printf("\nCopy complete.\n");
    return 0;
}


