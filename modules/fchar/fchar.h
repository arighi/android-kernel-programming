/*
 * f-char: fast character device
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Copyright (C) 2010 Andrea Righi <arighi@develer.com>
 */

#ifndef FCHAR_H

#ifndef __KERNEL__
#include <features.h>
#endif
#include <linux/types.h>
#include <linux/ioctl.h>

/* See Documentation/ioctl/ioctl-number.txt */
#define FCHAR_IOC_MAGIC		0xe0
#define FCHAR_IOCGSIZE		_IOR(FCHAR_IOC_MAGIC, 1, int)

#define FCHAR_IOC_MAX_NR	1

#endif /* FCHAR_H */
