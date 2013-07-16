#ifndef _TTYHUB_IOCTL_H
#define _TTYHUB_IOCTL_H
/* ttyhub
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

#define TTYHUB_IOCTL_TYPE_ID 0xFF

#define TTYHUB_SUBSYS_ENABLE _IOW(TTYHUB_IOCTL_TYPE_ID, 1, int)

#endif /* _TTYHUB_IOCTL_H */

