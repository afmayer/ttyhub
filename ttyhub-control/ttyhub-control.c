/* TTYHUB control
 * Copyright (c) 2013 Alexander F. Mayer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include "../modules/include/ttyhub_ioctl.h"

int main(int argc, char *argv[])
{
        int retVal;
        int fd;
        int ldisc = 29;
        struct timeval tv;
        char *pFilename = NULL;
        char filenamebuf[256];

        printf("TTYHUB control\n");

        if (argc != 2)
        {
                printf("Error: Missing TTY filename (e.g. 'ttyS0'"
                        " or '/dev/ttyS0')\n");
                return 1;
        }

        if (argv[1][0] == '/')
        {
                /* absolute path */
                pFilename = argv[1];
        }
        else
        {
                /* device filename without path */
                snprintf(filenamebuf, sizeof(filenamebuf), "/dev/%s", argv[1]);
                pFilename = filenamebuf;
        }

        fd = open(pFilename, O_RDONLY | O_NOCTTY);
        printf("open('%s') returned %d - errno = %d\n", pFilename, fd, errno);
        if (fd == -1)
                return 1;

        retVal = ioctl(fd, TIOCSETD, &ldisc);
        printf("ioctl(%d, TIOCSETD, %d) returned %d - errno = %d.\n", fd,
                ldisc, retVal, errno);
        if (retVal == -1)
                return 1;

        retVal = ioctl(fd, TTYHUB_SUBSYS_ENABLE, 0);
        printf("ioctl(%d, TTYHUB_SUBSYS_ENABLE, 0) returned %d - "
                "errno = %d.\n", fd, retVal, errno);
        if (retVal == -1)
                return 1;

        while (1)
        {
                tv.tv_sec = 1;
                tv.tv_usec = 0;
                select(0, NULL, NULL, NULL, &tv);
        }
        //write(fd, "Hi\n", 3);
}

