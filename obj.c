/*
** Copyright (C) 2011 EADS France
** stephane duverger <stephane.duverger@eads.net>
** nicolas bareil <nicolas.bareil@eads.net>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along
** with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
int xwrite(int fd, void *buf, int count) {
        int ret;
        asm(
	   "xchg  %%esi, %%ebx  \n"
	   "xchg  %%edi, %%ecx  \n"
	   "int   $0x80         \n"
	   "xchg  %%esi, %%ebx  \n"
	   "xchg  %%edi, %%ecx  \n"
	   :"=a"(ret)
	   :"a"(4),"S"(fd),"D"(buf),"d"(count)
	   :"cc", "memory");
        return ret;
}

int f1(int var)
{
	return var*2;
}

int func()
{
	f1(2);
	xwrite(1,"ld-hook\n", 8);
	return 1;
}

